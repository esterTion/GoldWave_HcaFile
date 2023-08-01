#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <gwaudio.h>
#include <mmsystem.h>

#include <stdio.h>
#include <stdlib.h>

#include <memory>
#include <string>

#include "../libcgss/src/lib/cgss_api.h"
#include "../libcgss/src/lib/kawashima/wave/wave_native.h"
#include "valueAsker_esterTion.h"

//#pragma comment(lib, "cgss.lib")
#pragma comment(lib, "valueAsker_esterTion.lib")

using namespace Gap;
using namespace Gerr;

constexpr wchar_t Extension[] = L"hca, acb";

constexpr int     MaxRate = 192000;

struct SavedCriKey_ {
  uint32_t k1;
  uint32_t k2;
};

static SavedCriKey_ SavedCriKey{};

/*
  Table listing all files types supported in this module (just one).
  Each element provides the type name, extension(s), and ability flags.
  The type name must not contain : / \ . leading characters or -
  see AudioCreate() for reason.
 */
static Table SampleTable{L"CRIWARE Audio File", aRead, Extension};

/*
  Prototype for plug-in constructor
 */
AudioFile* gdecl AudioCreate(const wchar_t* name);

/*
  Make an Interface structure that host program uses to create
  plug-ins in the module.
 */
static Interface HcaFileInterface{
  AudioVersion,
  1,
  &SampleTable,
  AudioCreate,
  nullptr     // No configuration function
};

// This function is called by the application to get the table
AudioInterfaceDll(void) {
  return &HcaFileInterface;
}

#ifdef _WIN32
#include <locale.h>
// Windows DLL entry point
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
  setlocale(LC_ALL, ".UTF8");
  return TRUE;
}
#endif

//---------------------------------------------------------------------------
/*
  A derived Format class must be created to hold format specific
  information, such as bitrate, channels, bits, sampling rate, etc.
 */

class HcaFormat : public Format {
public:
  wchar_t   description[100];
  int       channels, bits, rate;
  unsigned  flags;

  HcaFormat(void) : description{}, channels{2}, bits{16}, rate{0}, flags{fAnyRate} {}
  
  ~HcaFormat(void) override = default;

  // Functions to set general format parameters
  Error gdecl SetChannels(int c) {
    if (channels == 1 || channels == 2) {
      channels = c;
      return eNone;
    }
    else
      return eUnsupported;
  }

  Error gdecl SetRate(int r) {
    if (r >= 100 && r <= MaxRate) {
      rate = r;
      return eNone;
    }
    else
      return eUnsupported;
  }

  Error gdecl SetBitrate(int bitrate) { return eUnsupported; }

  // Functions to get format parameters
  unsigned gdecl Flags(void) const { return flags; }
  int gdecl Channels(void) const { return channels; }
  int gdecl Rate(void) const { return rate; }
  int gdecl Bitrate(void) const { return channels * bits * rate; }

  HcaFormat& operator=(const HcaFormat& format) {
    rate = format.rate;
    channels = format.channels;
    bits = format.bits;
    flags = format.flags;
    wcscpy_s(description, format.description);
    return *this;
  }

  // Used by host program to see if two formats are similar
  bool gdecl operator==(const Format& f) const {
    const HcaFormat* format;

    /*
      Dynamic casting is not safe between Borland and Microsoft, so
      just use Type pointer to make sure the format object belongs
      to this module.
     */
    if (f.Type() != Type())
      return false;

    format = dynamic_cast<const HcaFormat*>(&f);

    if (!format)
      return false;
    else
      return (format->rate == rate
        || (format->flags & fAnyRate) || (flags & fAnyRate))
      && format->bits == bits && format->channels == channels;
  }

  Format* gdecl Duplicate(void) const {
    HcaFormat* format = new HcaFormat;
    *format = *this;
    return format;
  }

  // Functions to get format text description
  const wchar_t* gdecl Type(void) const { return SampleTable.name; }
  const wchar_t* gdecl Description(void) {
    wchar_t ratetext[20];

    if (rate)
      swprintf(ratetext, 20, L", %dHz, %dkbps", rate, Bitrate() / 1000);
    else
      ratetext[0] = L'\0';

    swprintf(description, 100, L"PCM signed %d bit%ls, %ls", bits, ratetext,
      channels == 1 ? L"mono" : L"stereo");

    return description;
  }
  const wchar_t* gdecl Extension(void) const { return ::Extension; }
};

static std::string utf16ToUTF8(const wchar_t* s) {
  const int size = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, 0, nullptr);

  std::string res(size_t(size), '\0');
  ::WideCharToMultiByte(CP_UTF8, 0, s, -1, res.data(), size, 0, nullptr);

  return res;
}

enum class CriFileType {
  Hca = 1,
  Acb
};

Error DetectCriFile(const wchar_t* file, CriFileType* ftype = nullptr) {
  std::string file_s = utf16ToUTF8(file);
  try {
    cgss::CFileStream fileIn(file_s.c_str(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
    cgss::CHcaDecoderConfig decoderConfig;
    decoderConfig.decodeFunc = cgss::CDefaultWaveGenerator::Decode16BitS;
    decoderConfig.waveHeaderEnabled = FALSE;
    cgss::CHcaDecoder hcaDecoder(&fileIn, decoderConfig);
    HCA_INFO hcaInfo = hcaDecoder.GetHcaInfo();

    if (ftype) {
      *ftype = CriFileType::Hca;
    }
    return eNone;
  }
  catch (...) {}
  try {
    cgss::CFileStream fileStream(file_s.c_str(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
    cgss::CAcbFile acb(&fileStream, file_s.c_str());

    acb.Initialize();
    if (ftype) {
      *ftype = CriFileType::Acb;
    }
    return eNone;
  }
  catch (...) {}
  return eFormat;
}

//---------------------------------------------------------------------------
/*
  To create the File Format plug-in class:
    - Derive a class from the AudioFile base class.
    - Override all required or pure virtual functions.
 */

class HcaFile : public AudioFile
{
  // Input data
  HcaFormat inFormat;     // Input file format
  int64     length,       // Number of samples in file
            memWavOffset, // Byte offset to start of audio
            memWavSize;
  FILE*     inFile;       // Input file
  std::unique_ptr<uint8_t[]> memWavData;

  // Output data
  HcaFormat outFormat;    // Output file format

public:
  HcaFile(void) {
    inFile = 0;
    memWavData = 0;
    memWavSize = 0;
    length = memWavOffset = 0;
  }

  ~HcaFile(void) override {
    Close();
  }

  // Input member functions
  Error gdecl Open(const wchar_t* name, const Format* format = nullptr) override;
  int   gdecl Read(audio* data, int samples) override;
  Error gdecl Seek(int64 start) override;
  Error gdecl Close(void) override;

  // Output member functions
  Error gdecl Begin(const wchar_t* name, const Format& format) override;
  Error gdecl Write(const audio* data, int samples) override;
  Error gdecl End(void) override;

  int64           gdecl Length(void) { return length; }
  FormatList*     gdecl Formats(void);
  Format*         gdecl GetFormat(void);
  unsigned        gdecl Ability(void) { return SampleTable.abilities; }
  const wchar_t*  gdecl Name(void) { return SampleTable.name; }
};

//---------------------------------------------------------------------------
// Read member functions

Error gdecl HcaFile::Open(const wchar_t* name, const Format*) {
  if (!name)
    return eOpen;

  Close();

  CriFileType ftype;
  auto error = DetectCriFile(name, &ftype);

  if (error != eNone)
    return error;

  try {
    std::unique_ptr<cgss::IStream> hcaStream;
    cgss::CHcaDecoderConfig decoderConfig;
    decoderConfig.decodeFunc = cgss::CDefaultWaveGenerator::Decode16BitS;
    decoderConfig.waveHeaderEnabled = FALSE;
    if (ftype == CriFileType::Hca) {
      auto file_s = utf16ToUTF8(name);
      hcaStream = std::make_unique<cgss::CFileStream>(file_s.c_str(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
    }
    else if (ftype == CriFileType::Acb) {
      auto file_s = utf16ToUTF8(name);
      cgss::CFileStream fileStream(file_s.c_str(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
      cgss::CAcbFile acb(&fileStream, file_s.c_str());

      acb.Initialize();
      uint32_t internalCnt = 0, externalCnt = 0;

      auto intArchive = std::unique_ptr<cgss::CAfs2Archive>(acb.GetInternalAwb());
      if (intArchive)
        internalCnt = intArchive->GetFiles().size();
      auto extArchive = std::unique_ptr<cgss::CAfs2Archive>(acb.GetExternalAwb());
      if (extArchive)
        externalCnt = extArchive->GetFiles().size();
      uint32_t totalCnt = internalCnt + externalCnt;

      int32_t choice = 1, i = 1;
      if (totalCnt > 1) {
        int32_t j = 0;
        auto durations = std::make_unique<int32_t[]>(totalCnt);
        if (intArchive) {
          for (auto& entry : intArchive->GetFiles()) {
            auto& record = entry.second;
            auto hca = std::unique_ptr<cgss::CMemoryStream>(cgss::CAcbHelper::ExtractToNewStream(&fileStream, record.fileOffsetAligned, (uint32_t) record.fileSize));
            const auto isHca = cgss::CHcaFormatReader::IsPossibleHcaStream(hca.get());
            if (isHca) {
              cgss::CHcaDecoder hcaDecoder(hca.get(), decoderConfig);
              HCA_INFO hcaInfo = hcaDecoder.GetHcaInfo();
              cgss::WaveSampleSection wavSmpl = { 's', 'm', 'p', 'l', 0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0 };
              wavSmpl.loopStart;
              if (hcaInfo.loopExists) {
                  wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
                  wavSmpl.loopEnd = hcaInfo.loopEnd * 0x80 * 8;
              }
              durations[j++] = (2 * hcaInfo.channelCount) * (hcaInfo.blockCount * 0x80 * 8 +
                  (wavSmpl.loopEnd - wavSmpl.loopStart) * decoderConfig.loopCount) / hcaInfo.channelCount / (16 / 8) * 1000 / hcaInfo.samplingRate;
            }
            else {
              durations[j++] = 0;
            }
          }
        }
        if (extArchive) {
          cgss::CFileStream fs(extArchive->GetFileName(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
          for (auto& entry : extArchive->GetFiles()) {
            auto& record = entry.second;
            auto hca = std::unique_ptr<cgss::CMemoryStream>(cgss::CAcbHelper::ExtractToNewStream(&fs, record.fileOffsetAligned, (uint32_t) record.fileSize));
            const auto isHca = cgss::CHcaFormatReader::IsPossibleHcaStream(hca.get());
            if (isHca) {
              cgss::CHcaDecoder hcaDecoder(hca.get(), decoderConfig);
              HCA_INFO hcaInfo = hcaDecoder.GetHcaInfo();
              cgss::WaveSampleSection wavSmpl = { 's', 'm', 'p', 'l', 0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0 };
              wavSmpl.loopStart;
              if (hcaInfo.loopExists) {
                  wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
                  wavSmpl.loopEnd = hcaInfo.loopEnd * 0x80 * 8;
              }
              durations[j++] = (2 * hcaInfo.channelCount) * (hcaInfo.blockCount * 0x80 * 8 +
                  (wavSmpl.loopEnd - wavSmpl.loopStart) * decoderConfig.loopCount) / hcaInfo.channelCount / (16 / 8) * 1000 / hcaInfo.samplingRate;
            }
            else {
              durations[j++] = 0;
            }
          }
        }
        askTrackNo(&choice, totalCnt, durations.get());
      }
      std::unique_ptr<cgss::CFileStream> extFileStream;
      cgss::CAfs2Archive* archive;
      bool isInternal = (uint32_t) choice <= internalCnt;
      cgss::IStream* dataStream;
      if (isInternal) {
        archive = intArchive.get();
        dataStream = &fileStream;
      }
      else {
        archive = extArchive.get();
        choice -= internalCnt;
        extFileStream = std::make_unique<cgss::CFileStream>(archive->GetFileName(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
        dataStream = extFileStream.get();
      }
      for (auto& entry : archive->GetFiles()) {
        if (i++ != choice) continue;
        auto& record = entry.second;
        hcaStream.reset(cgss::CAcbHelper::ExtractToNewStream(dataStream, record.fileOffsetAligned, (uint32_t) record.fileSize));
        const auto isHca = cgss::CHcaFormatReader::IsPossibleHcaStream(hcaStream.get());
        if (!isHca) {
          Ask(L"Track is not hca stream\n\nAborting", Asker::Ok, Asker::Ok);
          Close();
          return eAbort;
        }
        break;
      }
      decoderConfig.cipherConfig.keyModifier = archive->GetHcaKeyModifier();
    }
    else {
      return eFormat;
    }
    if (!hcaStream) {
      // Unexpected
      Close();
      return eAbort;
    }
    uint32_t k1, k2;
    askCriKey(&k1, &k2, SavedCriKey.k1, SavedCriKey.k2);
    SavedCriKey.k1 = k1;
    SavedCriKey.k2 = k2;
    decoderConfig.cipherConfig.keyParts.key1 = k1;
    decoderConfig.cipherConfig.keyParts.key2 = k2;
    cgss::CHcaDecoder hcaDecoder(hcaStream.get(), decoderConfig);
    auto hcaInfo = hcaDecoder.GetHcaInfo();
    inFormat.bits = 16;
    inFormat.channels = hcaInfo.channelCount;
    inFormat.rate = hcaInfo.samplingRate;
    auto len = hcaDecoder.GetLength();
    memWavData = std::make_unique<uint8_t[]>(size_t(len));
    cgss::CMemoryStream memStream(memWavData.get(), len);
    uint32_t read = 1;
    constexpr uint32_t bufferSize = 1024;
    uint8_t buffer[bufferSize];
    while (read) {
      read = hcaDecoder.Read(buffer, bufferSize, 0, bufferSize);
      if (read) {
        memStream.Write(buffer, bufferSize, 0, read);
      }
    }
    length = len / ((uint64_t) inFormat.channels * 2);
    memWavOffset = 0;
    memWavSize = len;
  }
  catch (...) {
    error = eFormat;
  }

#if 0
  try {
    std::unique_ptr<cgss::IStream> hcaStream;
    cgss::CHcaDecoderConfig decoderConfig;
    decoderConfig.decodeFunc = cgss::CDefaultWaveGenerator::Decode16BitS;
    decoderConfig.waveHeaderEnabled = FALSE;
    if (ftype == CriFileType::Hca) {
      std::string file_s = utf16ToUTF8(name);
      hcaStream = std::make_unique<cgss::CFileStream>(file_s.c_str(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
    }
    else if (ftype == CriFileType::Acb) {
      std::string file_s = utf16ToUTF8(name);
      cgss::CFileStream fileStream(file_s.c_str(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
      cgss::CAcbFile acb(&fileStream, file_s.c_str());

      acb.Initialize();
      uint32_t internalCnt = 0, externalCnt = 0;

      auto intArchive = std::unique_ptr<cgss::CAfs2Archive>(acb.GetInternalAwb());
      if (intArchive) {
        internalCnt = intArchive->GetFiles().size();
      }
      auto extArchive = std::unique_ptr<cgss::CAfs2Archive>(acb.GetExternalAwb());
      if (extArchive) {
        externalCnt = extArchive->GetFiles().size();
      }
      uint32_t totalCnt = internalCnt + externalCnt;

      int32_t choice = 1, i = 1;
      if (totalCnt > 1) {
        int32_t j = 0;
        auto durations = std::make_unique<int32_t[]>(totalCnt * 4);
        if (intArchive) {
          for (auto& entry : intArchive->GetFiles()) {
            auto& record = entry.second;
            auto hca = std::unique_ptr<cgss::CMemoryStream>(cgss::CAcbHelper::ExtractToNewStream(&fileStream, record.fileOffsetAligned, (uint32_t) record.fileSize));
            const auto isHca = cgss::CHcaFormatReader::IsPossibleHcaStream(hca.get());
            if (isHca) {
              cgss::CHcaDecoder hcaDecoder(hca.get(), decoderConfig);
              HCA_INFO hcaInfo = hcaDecoder.GetHcaInfo();
              WaveSampleSection wavSmpl = {'s', 'm', 'p', 'l', 0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0};
              wavSmpl.loopStart;
              if (hcaInfo.loopExists) {
                wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
                wavSmpl.loopEnd = hcaInfo.loopEnd * 0x80 * 8;
              }
              int32_t len = (2 * hcaInfo.channelCount) * (hcaInfo.blockCount * 0x80 * 8 +
                (wavSmpl.loopEnd - wavSmpl.loopStart) * decoderConfig.loopCount) / hcaInfo.channelCount / (16 / 8) * 1000 / hcaInfo.samplingRate;
              durations[j++] = len;
            }
            else {
              durations[j++] = 0;
            }
          }
        }
        if (extArchive) {
          cgss::CFileStream fs(extArchive->GetFileName(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
          for (auto& entry : extArchive->GetFiles()) {
            auto& record = entry.second;
            auto hca = std::unique_ptr<cgss::CMemoryStream>(cgss::CAcbHelper::ExtractToNewStream(&fs, record.fileOffsetAligned, (uint32_t) record.fileSize));
            const auto isHca = cgss::CHcaFormatReader::IsPossibleHcaStream(hca.get());
            if (isHca) {
              cgss::CHcaDecoder hcaDecoder(hca.get(), decoderConfig);
              HCA_INFO hcaInfo = hcaDecoder.GetHcaInfo();
              WaveSampleSection wavSmpl = {'s', 'm', 'p', 'l', 0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0};
              wavSmpl.loopStart;
              if (hcaInfo.loopExists) {
                wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
                wavSmpl.loopEnd = hcaInfo.loopEnd * 0x80 * 8;
              }
              int32_t len = (2 * hcaInfo.channelCount) * (hcaInfo.blockCount * 0x80 * 8 +
                (wavSmpl.loopEnd - wavSmpl.loopStart) * decoderConfig.loopCount) / hcaInfo.channelCount / (16 / 8) * 1000 / hcaInfo.samplingRate;
              durations[j++] = len;
            }
            else {
              durations[j++] = 0;
            }
          }
        }
        askTrackNo(&choice, totalCnt, durations.get());
      }
      if (choice > totalCnt) {
        Ask(L"Invalid track selection", Asker::Ok, Asker::Ok);
        Close();
        return eAbort;
      }
      std::unique_ptr<cgss::CFileStream> extFileStream;
      cgss::CAfs2Archive* archive;
      bool isInternal = choice <= internalCnt;
      cgss::IStream* dataStream;
      if (isInternal) {
        archive = intArchive.get();
        dataStream = &fileStream;
      }
      else {
        archive = extArchive.get();
        choice -= internalCnt;
        extFileStream = std::make_unique<cgss::CFileStream>(archive->GetFileName(), cgss::FileMode::OpenExisting, cgss::FileAccess::Read);
        dataStream = extFileStream.get();
      }
      for (auto& entry : archive->GetFiles()) {
        if (i++ != choice) continue;
        auto& record = entry.second;
        hcaStream.reset(cgss::CAcbHelper::ExtractToNewStream(dataStream, record.fileOffsetAligned, (uint32_t) record.fileSize));
        const auto isHca = cgss::CHcaFormatReader::IsPossibleHcaStream(hcaStream.get());
        if (!isHca) {
          Ask(L"Track is not hca stream\n\nAborting", Asker::Ok, Asker::Ok);
          Close();
          return eAbort;
        }
        break;
      }
    }
    if (!hcaStream) {
      // ???
      Close();
      return eAbort;
    }
    uint32_t k1, k2;
    askCriKey(&k1, &k2, SavedCriKey.k1, SavedCriKey.k2);
    SavedCriKey.k1 = k1;
    SavedCriKey.k2 = k2;
    decoderConfig.cipherConfig.keyParts.key1 = k1;
    decoderConfig.cipherConfig.keyParts.key2 = k2;
    cgss::CHcaDecoder hcaDecoder(hcaStream.get(), decoderConfig);
    HCA_INFO hcaInfo = hcaDecoder.GetHcaInfo();

    WaveSampleSection wavSmpl = {'s', 'm', 'p', 'l', 0x3C, 0, 0, 0, 0x3C, 0, 0, 0, 1, 0x18, 0, 0, 0, 0, 0, 0};
    wavSmpl.loopStart;
    if (hcaInfo.loopExists) {
      wavSmpl.loopStart = hcaInfo.loopStart * 0x80 * 8 + hcaInfo.fmtR02; // fmtR02 is muteFooter
      wavSmpl.loopEnd = hcaInfo.loopEnd * 0x80 * 8;
    }
    len = (2 * hcaInfo.channelCount) * (hcaInfo.blockCount * 0x80 * 8 +
      (wavSmpl.loopEnd - wavSmpl.loopStart) * decoderConfig.loopCount);

    inFormat.bits = 16;
    inFormat.channels = hcaInfo.channelCount;
    inFormat.rate = hcaInfo.samplingRate;

    memWavData = std::make_unique<uint8_t[]>(len);
    cgss::CMemoryStream memStream(memWavData.get(), len);
    uint32_t read = 1;
    static const uint32_t bufferSize = 1024;
    uint8_t buffer[bufferSize];

    while (read > 0) {
      read = hcaDecoder.Read(buffer, bufferSize, 0, bufferSize);

      if (read > 0) {
        memStream.Write(buffer, bufferSize, 0, read);
      }
    }

    memWavSize = len;
    length = len / inFormat.channels / (inFormat.bits / 8);
  }
  catch (const cgss::CException& ex) {
    error = eFormat;
  }
#endif

  return error;
}

// Read audio data from file and convert to native 'audio' type
int gdecl HcaFile::Read(audio* dest, int samples) {
  if (!memWavData)
    return -eForbidden;

  auto ptr = memWavData.get() + memWavOffset;

  int total = 0;
  while (samples--) {
    if (memWavOffset + (int64) inFormat.channels * 2 > memWavSize)
      break;
    for (auto i = 0; i < inFormat.channels; ++i, ptr += 2, memWavOffset += 2) {
      auto val = uint16_t(ptr[0]) | uint16_t(ptr[1]) << 8;
      *dest++ = (float) int16_t(val) / 0x7fff;
    }
    total++;
  }
  return total;
}

// Seek to sample position within the input stream
Error gdecl HcaFile::Seek(int64 position) {
  // Seek only applies to input.  If an input file is not open, return error
  if (!memWavData)
    return eForbidden;

  // Convert samples to bytes
  position *= inFormat.channels * 2;

  // Note: limited to 2GB on 32-bit systems
  if (position >= memWavSize)
    return eSeek;
  
  memWavOffset = position;

  return eNone;
}

// Close input file
Error gdecl HcaFile::Close(void) {
  if (!memWavData)
    return eForbidden;
  memWavData.reset();
  length = 0;
  memWavOffset = 0;

  // Reset format to default
  inFormat = HcaFormat();

  return eNone;
}

Error gdecl HcaFile::Begin(const wchar_t* name, const Format& f) {
  return eUnsupported;
}

// Write audio data to file in the correct format - either 8 or 16 bit
Error gdecl HcaFile::Write(const audio* data, int samples) {
  return eUnsupported;
}

// Close output file by writing RIFF blocks
Error gdecl HcaFile::End(void) {
  return eUnsupported;
}

//---------------------------------------------------------------------
// Wave FormatList functions

/*
  Only 4 combinations of attributes are supported:
    PCM signed 8 bit, mono
    PCM signed 8 bit, stereo
    PCM signed 16 bit, mono   (default)
    PCM signed 16 bit, stereo (default)
  To create this list, derive a class from FormatList and
  initialize an array of 4 HcaFormat objects with
  the above attributes.

  Note that a pointer to a static structure MUST NOT be
  returned since the application may modify the Format
  objects to set rates, channels, or bitrates.
 */

class HcaList : public FormatList {
  HcaFormat format[4];
public:
  HcaList(void) {
    count = sizeof(format) / sizeof(*format);

    // Setup 8-bit mono and stereo, 16-bit mono and stereo
    format[0].channels = format[2].channels = 1;
    format[0].bits = format[1].bits = 8;

    // One default format must be provided each for mono and stereo

    // Set 16-bit formats as default
    format[2].flags |= Format::fDefault;
    format[3].flags |= Format::fDefault;
  }

  Format* gdecl operator[](int i) {
    if (i >= 0 && i < count)
      return format + i;
    else
      return nullptr;
  }
};

// Create a FileList and pass it back to host program
FormatList* gdecl HcaFile::Formats(void) {
  // Host program will Destroy() this object
  return new HcaList;
}

Format* gdecl HcaFile::GetFormat(void) {
  // Return current format
  return inFormat.Duplicate();
}

//---------------------------------------------------------------------------
/*
  HcaFile Format plug-in constructor.  Takes a string containing the filename
  or type name and creates an appropriate plug-in or returns 0 if
  it does not recognize the file format or type name.
 */
AudioFile* gdecl AudioCreate(const wchar_t* name) {
  if (!name)
    return nullptr;

  // Is it a filename?  Host program provides full pathname
  // Should do a wcsstr( name, L"://" ) for URL detection
  if (name[1] == L':' || name[0] == L'\\'
    || name[0] == L'/' || name[0] == L'.')
  {
    Error error;
    error = DetectCriFile(name);

    // Does the file contain Wave audio?
    if (error == eNone)
      return new HcaFile;
  }
  else if (wcscmp(name, SampleTable.name) == 0)
    return new HcaFile;

  return nullptr;
};

//---------------------------------------------------------------------------
