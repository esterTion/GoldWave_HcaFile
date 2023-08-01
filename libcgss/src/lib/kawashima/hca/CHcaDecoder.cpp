#include <algorithm>
#include "CHcaDecoder.h"
#include "internal/CHcaAth.h"
#include "internal/CHcaChannel.h"
#include "internal/CHcaCipher.h"
#include "internal/CHcaData.h"
#include "../../common/quick_utils.h"
#include "hca_utils.h"
#include "../../takamori/exceptions/CArgumentException.h"
#include "../wave/wave_native.h"
#include "../../takamori/streams/CMemoryStream.h"

#ifdef _MSC_VER
#undef max
#undef min
#endif

CGSS_NS_BEGIN

    CHcaDecoder::CHcaDecoder(IStream *stream)
        : MyClass(stream, HCA_DECODER_CONFIG()) {
    }

    CHcaDecoder::CHcaDecoder(IStream *stream, const HCA_DECODER_CONFIG &decoderConfig)
        : MyBase(stream) {
        _cipher = nullptr;
        _ath = nullptr;
        for (auto i = 0; i < ChannelCount; ++i) {
            _channels[i] = nullptr;
        }
        _waveHeaderBuffer = _hcaBlockBuffer = nullptr;
        _waveHeaderSize = _waveBlockSize = 0;
        _position = 0;
        clone(decoderConfig, _decoderConfig);
        InitializeExtra();
    }

    CHcaDecoder::~CHcaDecoder() {
        for (const auto &v : _decodedBlocks) {
            delete[] v.second;
        }
        _decodedBlocks.clear();
        if (_waveHeaderBuffer) {
            delete[] _waveHeaderBuffer;
            _waveHeaderBuffer = nullptr;
        }
        if (_hcaBlockBuffer) {
            delete[] _hcaBlockBuffer;
            _hcaBlockBuffer = nullptr;
        }
        if (_ath) {
            delete _ath;
            _ath = nullptr;
        }
        if (_cipher) {
            delete _cipher;
            _cipher = nullptr;
        }
        if (_channels) {
            for (auto i = 0; i < ChannelCount; ++i) {
                delete _channels[i];
                _channels[i] = nullptr;
            }
        }
        if (_channels_vgmstream) {
            delete _channels_vgmstream;
        }
    }

    void CHcaDecoder::InitializeExtra() {
        auto &hcaInfo = _hcaInfo;

        // Initialize adjustment and cipher tables.
        _ath = new CHcaAth();
        if (!_ath->Init(hcaInfo.athType, hcaInfo.samplingRate)) {
            throw CException();
        }
        auto &cipherConfig = _decoderConfig.cipherConfig;
        cipherConfig.cipherType = hcaInfo.cipherType;
        auto hcaCipherConfig = CHcaCipherConfig(cipherConfig.key, cipherConfig.keyModifier);
        hcaCipherConfig.cipherType = hcaInfo.cipherType;
        _cipher = new CHcaCipher(hcaCipherConfig);

        // Prepare the channel decoders.
        memset(_channels, 0, sizeof(_channels));
        uint8_t r[0x10];
        memset(r, 0, 0x10);
        uint32_t b = hcaInfo.channelCount / hcaInfo.compR03;
        if (hcaInfo.compR07 && b > 1) {
            auto *c = r;
            for (auto i = 0; i < hcaInfo.compR03; ++i, c += b) {
                switch (b) {
                    case 2:
                    case 3:
                        c[0] = 1;
                        c[1] = 2;
                        break;
                    case 4:
                        c[0] = 1;
                        c[1] = 2;
                        if (hcaInfo.compR04 == 0) {
                            c[2] = 1;
                            c[3] = 2;
                        }
                        break;
                    case 5:
                        c[0] = 1;
                        c[1] = 2;
                        if (hcaInfo.compR04 <= 2) {
                            c[3] = 1;
                            c[4] = 2;
                        }
                        break;
                    case 6:
                    case 7:
                        c[0] = 1;
                        c[1] = 2;
                        c[4] = 1;
                        c[5] = 2;
                        // Fall through
                    case 8:
                        c[6] = 1;
                        c[7] = 2;
                        break;
                    default:
                        throw CArgumentException();
                }
            }
        }
        auto *channels = _channels;
        stChannel* channels_vgmstream = new stChannel[hcaInfo.channelCount];
        for (auto i = 0; i < hcaInfo.channelCount; ++i) {
            channels[i] = new CHcaChannel();
            channels[i]->type = r[i];
            channels[i]->value3 = &channels[i]->value[hcaInfo.compR06 + hcaInfo.compR07];
            channels[i]->count = hcaInfo.compR06 + ((r[i] != 2) ? hcaInfo.compR07 : 0);

            memset(&channels_vgmstream[i], 0, sizeof(stChannel));
            channels_vgmstream[i].type = (channel_type_t)channels[i]->type;
            channels_vgmstream[i].coded_count = (channels[i]->type != STEREO_SECONDARY) ?
                hcaInfo.compR06 + hcaInfo.compR07 :
                hcaInfo.compR06;
        }
        _channels_vgmstream = channels_vgmstream;
    }

    uint32_t CHcaDecoder::GetWaveHeaderSize() {
        if (_waveHeaderSize) {
            return _waveHeaderSize;
        }

        auto &hcaInfo = _hcaInfo;
        uint32_t sizeNeeded = sizeof(WaveRiffSection);
        if (hcaInfo.loopExists && !WaveSettings::SoftLoop) {
            sizeNeeded += sizeof(WaveSampleSection);
        }
        if (hcaInfo.commentLength > 0) {
            uint32_t noteSize = 4 + hcaInfo.commentLength + 1;
            // Pad by 4
            if (noteSize & 3) {
                noteSize += 4 - (noteSize & 3);
            }
            sizeNeeded += 8 + noteSize;
        }
        sizeNeeded += sizeof(WaveDataSection);
        _waveHeaderSize = sizeNeeded;
        return sizeNeeded;
    }

    const uint8_t *CHcaDecoder::GenerateWaveHeader() {
        if (_waveHeaderBuffer) {
            return _waveHeaderBuffer;
        }
        const auto &hcaInfo = _hcaInfo;
        const auto headerSize = GetWaveHeaderSize();
        auto headerBuffer = _waveHeaderBuffer = new uint8_t[headerSize];
        memset(headerBuffer, 0, headerSize);

        WaveRiffSection wavRiff = {'R', 'I', 'F', 'F', 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 0x10, 0, 0, 0, 0, 0, 0};
        WaveSampleSection wavSmpl = {'s', 'm', 'p', 'l', 0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0};
        WaveNoteSection wavNote = {'n', 'o', 't', 'e', 0, 0};
        WaveDataSection wavData = {'d', 'a', 't', 'a', 0};

        wavRiff.fmtType = static_cast<uint16_t>((WaveSettings::BitPerChannel > 0) ? 1 : 3);
        wavRiff.fmtChannelCount = static_cast<uint16_t>(hcaInfo.channelCount);
        wavRiff.fmtBitCount = static_cast<uint16_t>((WaveSettings::BitPerChannel > 0) ? WaveSettings::BitPerChannel : 32);
        wavRiff.fmtSamplingRate = hcaInfo.samplingRate;
        wavRiff.fmtSamplingSize = static_cast<uint16_t>(wavRiff.fmtBitCount / 8 * wavRiff.fmtChannelCount);
        wavRiff.fmtSamplesPerSec = wavRiff.fmtSamplingRate * wavRiff.fmtSamplingSize;
        if (hcaInfo.loopExists) {
            wavSmpl.samplePeriod = static_cast<uint32_t>(1 / (double)wavRiff.fmtSamplingRate * 1000000000);
            wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
            wavSmpl.loopEnd = hcaInfo.loopEnd * 0x80 * 8;
            wavSmpl.loopPlayCount = (hcaInfo.loopR01 == 0x80) ? 0 : hcaInfo.loopR01;
        } else if (WaveSettings::SoftLoop) {
            wavSmpl.loopStart = 0;
            wavSmpl.loopEnd = hcaInfo.blockCount * 0x80 * 8;
        }
        if (hcaInfo.commentLength > 0) {
            wavNote.noteSize = 4 + hcaInfo.commentLength + 1;
            if (wavNote.noteSize & 3) {
                wavNote.noteSize += 4 - (wavNote.noteSize & 3);
            }
        }
        wavData.dataSize = wavRiff.fmtSamplingSize * (hcaInfo.blockCount * 0x80 * 8 +
                                                      (wavSmpl.loopEnd - wavSmpl.loopStart) * _decoderConfig.loopCount);
        wavRiff.riffSize = static_cast<uint32_t>(0x1C + ((hcaInfo.loopExists && !WaveSettings::SoftLoop) ? sizeof(wavSmpl) : 0) +
                                                 (hcaInfo.commentLength > 0 ? 8 + wavNote.noteSize : 0) + sizeof(wavData) +
                                                 wavData.dataSize);

        CMemoryStream memoryStream(headerBuffer, headerSize);
#define WRITE_STRUCT(var) memoryStream.Write(&var, sizeof(var), 0, sizeof(var))
        WRITE_STRUCT(wavRiff);
        if (hcaInfo.loopExists && !WaveSettings::SoftLoop) {
            WRITE_STRUCT(wavSmpl);
        }
        if (hcaInfo.commentLength > 0) {
            WRITE_STRUCT(wavNote);
            memoryStream.Write(hcaInfo.comment, hcaInfo.commentLength + 1, 0, hcaInfo.commentLength + 1);
        }
        WRITE_STRUCT(wavData);
#undef WRITE_STRUCT

        return headerBuffer;
    }

    uint32_t CHcaDecoder::GetWaveBlockSize() {
        if (_waveBlockSize) {
            return _waveBlockSize;
        }
        uint32_t audioBitPerChannel = WaveSettings::BitPerChannel != 0 ? WaveSettings::BitPerChannel : sizeof(float);
        uint32_t waveBlockSize = 0x80 * (audioBitPerChannel / sizeof(uint8_t)) * _hcaInfo.channelCount;
        _waveBlockSize = waveBlockSize;
        return waveBlockSize;
    }

    const uint8_t *CHcaDecoder::DecodeBlock(uint32_t blockIndex) {
        auto &decodedBlocks = _decodedBlocks;
        {
            const auto decodedItem = decodedBlocks.find(blockIndex);
            if (decodedItem != decodedBlocks.cend()) {
                return decodedItem->second;
            }
        }

        auto stream = _baseStream;
        const auto &hcaInfo = _hcaInfo;
        const auto waveBlockSize = GetWaveBlockSize();
        const auto *channels = _channels;

        auto hcaBlockBuffer = _hcaBlockBuffer ? _hcaBlockBuffer : new uint8_t[hcaInfo.blockSize];
        _hcaBlockBuffer = hcaBlockBuffer;

        stream->Seek(hcaInfo.dataOffset + hcaInfo.blockSize * blockIndex, StreamSeekOrigin::Begin);
        auto actualRead = stream->Read(hcaBlockBuffer, hcaInfo.blockSize, 0, hcaInfo.blockSize);
        if (actualRead < hcaInfo.blockSize) {
            throw CException(CGSS_OP_DECODE_FAILED);
        }

        // Compute block checksum.
        if (ComputeChecksum(hcaBlockBuffer, hcaInfo.blockSize, 0) != 0) {
            throw CException(CGSS_OP_CHECKSUM_ERROR);
        }

        // Decrypt block if needed.
        _cipher->Decrypt(hcaBlockBuffer, hcaInfo.blockSize);

        CHcaData data(hcaBlockBuffer, hcaInfo.blockSize, hcaInfo.blockSize);

        const auto magic = data.GetBit(16);
        if (magic != 0xffff) {
            throw CException(CGSS_OP_DECODE_FAILED);
        }

        // Actual decoding process.
        /*
        * Seems got bug with acb 1.40, switch to use code from vgmstream
        auto a = (data.GetBit(9) << 8) - data.GetBit(7);
        for (auto i = 0; i < hcaInfo.channelCount; ++i) {
            CHcaChannel::Decode1(channels[i], &data, hcaInfo.compR09, a, _ath->GetTable());
        }
        for (auto i = 0; i < 8; ++i) {
            for (auto j = 0; j < hcaInfo.channelCount; ++j) {
                CHcaChannel::Decode2(channels[j], &data);
            }
            for (auto j = 0; j < hcaInfo.channelCount; ++j) {
                CHcaChannel::Decode3(channels[j], hcaInfo.compR09, hcaInfo.compR08, hcaInfo.compR07 + hcaInfo.compR06, hcaInfo.compR05);
            }
            for (auto j = 0; j < hcaInfo.channelCount - 1; ++j) {
                CHcaChannel::Decode4(channels[j], channels[j + 1], i, hcaInfo.compR05 - hcaInfo.compR06, hcaInfo.compR06, hcaInfo.compR07);
            }
            for (auto j = 0; j < hcaInfo.channelCount; ++j) {
                CHcaChannel::Decode5(channels[j], i);
            }
        }
        */
        
        //clHCA_DecodeBlock_unpack
        clData br;
        unsigned short sync;
        unsigned int subframe, ch;
        bitreader_init(&br, hcaBlockBuffer, hcaInfo.blockSize);
        bitreader_read(&br, 16);
        unsigned int hcaInfoVersion = hcaInfo.versionMajor * 0x100 + hcaInfo.versionMinor;
        stChannel* channels_vgmstream = _channels_vgmstream;
        /* unpack frame values */
        {
            /* lib saves this in the struct since they can stop/resume subframe decoding */
            unsigned int frame_acceptable_noise_level = bitreader_read(&br, 9);
            unsigned int frame_evaluation_boundary = bitreader_read(&br, 7);

            unsigned int packed_noise_level = (frame_acceptable_noise_level << 8) - frame_evaluation_boundary;

            for (ch = 0; ch < hcaInfo.channelCount; ch++) {
                int err = unpack_scalefactors(&channels_vgmstream[ch], &br, hcaInfo.compR09, hcaInfoVersion);
                if (err < 0)
                    throw CException(CGSS_OP_DECODE_FAILED);

                unpack_intensity(&channels_vgmstream[ch], &br, hcaInfo.compR09, hcaInfoVersion);

                calculate_resolution(&channels_vgmstream[ch], packed_noise_level, _ath->GetTable(), hcaInfo.compR01, hcaInfo.compR02);

                calculate_gain(&channels_vgmstream[ch]);
            }
        }

        /* lib seems to use a state value to skip parts (unpacking/subframe N/etc) as needed */
        for (subframe = 0; subframe < 8; subframe++) {

            /* unpack channel data and get dequantized spectra */
            for (ch = 0; ch < hcaInfo.channelCount; ch++) {
                dequantize_coefficients(&channels_vgmstream[ch], &br, subframe);
            }

            /* original code transforms subframe here, but we have it for later */
        }

        //clHCA_DecodeBlock_transform
        if (br.bit >= 0) {
            for (subframe = 0; subframe < 8; subframe++) {
                /* restore missing bands from spectra */
                for (ch = 0; ch < hcaInfo.channelCount; ch++) {
                    reconstruct_noise(&channels_vgmstream[ch], hcaInfo.compR01, /*hcaInfo.ms_stereo*/0, (unsigned int*)&hcaInfo.random, subframe);

                    reconstruct_high_frequency(&channels_vgmstream[ch], hcaInfo.compR09, hcaInfo.compR08,
                        hcaInfo.compR07, hcaInfo.compR06, hcaInfo.compR05, hcaInfoVersion, subframe);
                }

                /* restore missing joint stereo bands */
                if (hcaInfo.compR07 > 0) {
                    for (ch = 0; ch < hcaInfo.channelCount - 1; ch++) {
                        apply_intensity_stereo(&channels_vgmstream[ch], subframe, hcaInfo.compR06, hcaInfo.compR05);

                        apply_ms_stereo(&channels_vgmstream[ch], /*hcaInfo.ms_stereo*/0, hcaInfo.compR06, hcaInfo.compR05, subframe);
                    }
                }

                /* apply imdct */
                for (ch = 0; ch < hcaInfo.channelCount; ch++) {
                    imdct_transform(&channels_vgmstream[ch], subframe);
                }
            }
        }


        // Generate wave data.
        const auto waveBlockBuffer = new uint8_t[waveBlockSize];
        const auto decodeFunc = _decoderConfig.decodeFunc;
        uint32_t cursor = 0;
        if (decodeFunc) {
            for (auto i = 0; i < 8; ++i) {
                for (auto j = 0; j < 0x80; ++j) {
                    for (auto k = 0; k < hcaInfo.channelCount; ++k) {
                        auto f = channels_vgmstream[k].wave[i][j] * hcaInfo.rvaVolume;
                        f = clamp(f, -1.0f, 1.0f);
                        cursor = decodeFunc(f, waveBlockBuffer, cursor);
                    }
                }
            }
        }

        decodedBlocks[blockIndex] = waveBlockBuffer;
        return waveBlockBuffer;
    }

    uint64_t CHcaDecoder::GetPosition() {
        return _position;
    }

    void CHcaDecoder::SetPosition(uint64_t value) {
        _position = value;
    }

    uint64_t CHcaDecoder::MapLoopedPosition(uint64_t linearPosition) {
        const auto &decoderConfig = _decoderConfig;
        const auto waveHeaderSize = decoderConfig.waveHeaderEnabled ? GetWaveHeaderSize() : 0;
        const auto &hcaInfo = _hcaInfo;
        if (!hcaInfo.loopExists || !decoderConfig.loopEnabled) {
            return linearPosition;
        }

        // Now, linearPosition points to the audio data.
        const auto waveBlockSize = GetWaveBlockSize();
        const auto beforeLoopStart = hcaInfo.loopStart > 1 ? hcaInfo.loopStart - 1 : 0;
        const auto inLoop = hcaInfo.loopEnd - hcaInfo.loopStart + 1;
        if (linearPosition <= waveHeaderSize + (beforeLoopStart + inLoop) * waveBlockSize) {
            return linearPosition;
        }

        if (decoderConfig.loopCount == 0) {
            throw CArgumentException("CHcaDecoder::MapLoopedPosition");
        }
        auto loopCount = (linearPosition - waveHeaderSize - beforeLoopStart * waveBlockSize) / (inLoop * waveBlockSize);
        loopCount = std::min(static_cast<uint32_t>(loopCount), decoderConfig.loopCount);
        return linearPosition - loopCount * inLoop * waveBlockSize - waveHeaderSize;
    }

    uint64_t CHcaDecoder::GetLength() {
        const auto &hcaInfo = _hcaInfo;
        const auto &decoderConfig = _decoderConfig;
        if (hcaInfo.loopExists && decoderConfig.loopEnabled) {
            if (decoderConfig.loopCount == 0) {
                throw CArgumentException("CHcaDecoder::GetLength");
            }
            uint64_t total = 0;
            if (decoderConfig.waveHeaderEnabled) {
                total += GetWaveHeaderSize();
            }
            const auto beforeLoopStart = hcaInfo.loopStart > 1 ? hcaInfo.loopStart - 1 : 0;
            const auto afterLoopEnd = hcaInfo.loopEnd < hcaInfo.blockCount - 1 ? hcaInfo.blockCount - 1 - hcaInfo.loopEnd : 0;
            const auto inLoop = hcaInfo.loopEnd - hcaInfo.loopStart + 1;
            total += (beforeLoopStart + afterLoopEnd) * GetWaveBlockSize();
            total += inLoop * decoderConfig.loopCount * GetWaveBlockSize();
            return total;
        } else {
            if (decoderConfig.waveHeaderEnabled) {
                return GetWaveHeaderSize() + GetWaveBlockSize() * hcaInfo.blockCount;
            } else {
                return GetWaveBlockSize() * hcaInfo.blockCount;
            }
        }
    }

    uint32_t CHcaDecoder::Read(void *buffer, uint32_t bufferSize, size_t offset, uint32_t count) {
        if (!buffer) {
            throw CArgumentException("CHcaDecoder::Read");
        }
        bufferSize = std::max(0u, std::min(count, static_cast<uint32_t>(bufferSize - offset)));
        if (bufferSize == 0) {
            return bufferSize;
        }
        auto byteBuffer = static_cast<uint8_t *>(buffer);

        const auto &decoderConfig = _decoderConfig;
        auto streamPosition = GetPosition();
        auto mappedPosition = MapLoopedPosition(streamPosition);
        const auto waveStreamLength = GetLength();
        if (mappedPosition >= waveStreamLength) {
            return 0;
        }

        const auto waveHeaderSize = decoderConfig.waveHeaderEnabled ? GetWaveHeaderSize() : 0;
        uint32_t totalRead = 0;
        if (mappedPosition < waveHeaderSize) {
            const auto headerLeftLength = static_cast<size_t>(waveHeaderSize - mappedPosition);
            const auto headerCopyLength = std::min(static_cast<uint32_t>(headerLeftLength), bufferSize);
            memcpy(byteBuffer + offset, GenerateWaveHeader() + mappedPosition, headerCopyLength);
            streamPosition += headerCopyLength;
            if (bufferSize <= headerCopyLength) {
                SetPosition(streamPosition);
                return bufferSize;
            }
            bufferSize -= headerCopyLength;
            offset += headerCopyLength;
            totalRead += headerCopyLength;
            mappedPosition += headerCopyLength;
        }

        // Now mappedPosition is pointed inside audio data.
        const auto waveBlockSize = GetWaveBlockSize();
        while (bufferSize > 0 && mappedPosition < waveStreamLength) {
            const auto blockIndex = static_cast<uint32_t>((mappedPosition - waveHeaderSize) / waveBlockSize);
            const auto startOffset = (mappedPosition - waveHeaderSize) % waveBlockSize;
            const auto blockData = DecodeBlock(blockIndex);
            const auto copyLength = std::min(waveStreamLength - mappedPosition, std::min(waveBlockSize - startOffset, static_cast<uint64_t>(bufferSize)));
            memcpy(byteBuffer + offset, blockData + startOffset, static_cast<size_t>(copyLength));
            streamPosition += copyLength;
            bufferSize -= copyLength;
            offset += copyLength;
            totalRead += copyLength;
            mappedPosition += copyLength;
        }

        SetPosition(streamPosition);
        return totalRead;
    }

CGSS_NS_END
