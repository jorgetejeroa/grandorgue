/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiFileTimeScaler.h"

#include <cmath>
#include <cstring>

#include <wx/file.h>

static const size_t CHUNK_HEADER_LEN = 8;

static uint32_t read_be32(const std::vector<uint8_t> &data, size_t pos) {
  return ((uint32_t)data[pos] << 24) | ((uint32_t)data[pos + 1] << 16)
    | ((uint32_t)data[pos + 2] << 8) | (uint32_t)data[pos + 3];
}

static void write_be32(std::vector<uint8_t> &data, size_t pos, uint32_t v) {
  data[pos] = (v >> 24) & 0xFF;
  data[pos + 1] = (v >> 16) & 0xFF;
  data[pos + 2] = (v >> 8) & 0xFF;
  data[pos + 3] = v & 0xFF;
}

/**
 * Reads a variable length quantity
 * @return false if the data ends before the quantity
 */
static bool read_vlq(
  const std::vector<uint8_t> &data, size_t end, size_t &pos, uint32_t &outV) {
  bool isComplete = false;
  unsigned nBytes = 0;

  outV = 0;
  while (!isComplete && pos < end && nBytes < 4) {
    const uint8_t b = data[pos++];

    outV = (outV << 7) | (b & 0x7F);
    isComplete = !(b & 0x80);
    nBytes++;
  }
  return isComplete;
}

static void write_vlq(std::vector<uint8_t> &out, uint32_t v) {
  uint8_t buf[5];
  unsigned n = 0;

  buf[n++] = v & 0x7F;
  while (v >>= 7)
    buf[n++] = (v & 0x7F) | 0x80;
  while (n)
    out.push_back(buf[--n]);
}

static void copy_bytes(
  const std::vector<uint8_t> &data,
  size_t pos,
  size_t len,
  std::vector<uint8_t> &out) {
  out.insert(out.end(), data.begin() + pos, data.begin() + pos + len);
}

/**
 * Scales the delta times of one track (the chunk content without header)
 * @return false if the track is malformed
 */
static bool scale_track(
  const std::vector<uint8_t> &data,
  size_t pos,
  size_t end,
  double timeFactor,
  std::vector<uint8_t> &out) {
  bool isOk = true;
  uint64_t absTime = 0;
  uint64_t newAbsTime = 0;
  uint8_t runningStatus = 0;

  while (isOk && pos < end) {
    uint32_t delta;

    isOk = read_vlq(data, end, pos, delta) && pos < end;
    if (isOk) {
      absTime += delta;

      const uint64_t scaledAbsTime
        = (uint64_t)std::llround((double)absTime * timeFactor);
      const uint64_t newDelta
        = scaledAbsTime > newAbsTime ? scaledAbsTime - newAbsTime : 0;

      isOk = newDelta <= 0x0FFFFFFF;
      newAbsTime += newDelta;
      write_vlq(out, (uint32_t)newDelta);
    }
    if (isOk) {
      // with running status the status byte is omitted and the previous one
      // of a channel message is used
      const bool hasStatusByte = data[pos] & 0x80;
      const uint8_t status = hasStatusByte ? data[pos] : runningStatus;

      if (hasStatusByte) {
        out.push_back(status);
        pos++;
      }
      isOk = status & 0x80;

      if (!isOk) {
        // data byte without any status
      } else if (status == 0xFF) {
        // meta event: type, length, data
        uint32_t len;
        const size_t start = pos;

        runningStatus = 0;
        pos++; // type
        isOk = pos < end && read_vlq(data, end, pos, len) && pos + len <= end;
        if (isOk) {
          copy_bytes(data, start, pos + len - start, out);
          pos += len;
        }
      } else if (status == 0xF0 || status == 0xF7) {
        // system exclusive: length, data
        uint32_t len;
        const size_t start = pos;

        runningStatus = 0;
        isOk = read_vlq(data, end, pos, len) && pos + len <= end;
        if (isOk) {
          copy_bytes(data, start, pos + len - start, out);
          pos += len;
        }
      } else {
        // channel message with one or two data bytes
        const uint8_t type = status & 0xF0;
        const size_t len = (type == 0xC0 || type == 0xD0) ? 1 : 2;

        runningStatus = status;
        isOk = pos + len <= end;
        if (isOk) {
          copy_bytes(data, pos, len, out);
          pos += len;
        }
      }
    }
  }
  return isOk;
}

bool GOMidiFileTimeScaler::scaleData(
  const std::vector<uint8_t> &data,
  double timeFactor,
  std::vector<uint8_t> &outData) {
  bool isOk = timeFactor > 0 && data.size() >= CHUNK_HEADER_LEN + 6
    && !memcmp(data.data(), "MThd", 4);
  size_t pos = 0;

  outData.clear();
  while (isOk && pos < data.size()) {
    isOk = pos + CHUNK_HEADER_LEN <= data.size();
    if (isOk) {
      const uint32_t len = read_be32(data, pos + 4);
      const size_t contentPos = pos + CHUNK_HEADER_LEN;
      // tolerate a last chunk with a wrong length
      const size_t contentEnd
        = contentPos + len <= data.size() ? contentPos + len : data.size();

      if (!memcmp(&data[pos], "MTrk", 4)) {
        const size_t outHeaderPos = outData.size();

        copy_bytes(data, pos, CHUNK_HEADER_LEN, outData);
        isOk = scale_track(data, contentPos, contentEnd, timeFactor, outData);
        if (isOk)
          write_be32(
            outData,
            outHeaderPos + 4,
            (uint32_t)(outData.size() - outHeaderPos - CHUNK_HEADER_LEN));
      } else
        // the header and unknown chunks are copied unchanged
        copy_bytes(data, pos, contentEnd - pos, outData);
      pos = contentEnd;
    }
  }
  return isOk;
}

bool GOMidiFileTimeScaler::scaleFile(
  const wxString &inPath, const wxString &outPath, double timeFactor) {
  bool isOk = false;
  std::vector<uint8_t> data;
  std::vector<uint8_t> outData;
  wxFile inFile;

  if (inFile.Open(inPath, wxFile::read)) {
    const wxFileOffset len = inFile.Length();

    if (len > 0) {
      data.resize(len);
      isOk = inFile.Read(data.data(), len) == len;
    }
    inFile.Close();
  }
  isOk = isOk && scaleData(data, timeFactor, outData);
  if (isOk) {
    // write to a temporary file first, so the original is never left damaged
    const wxString tmpPath = outPath + wxT(".tmp");
    wxFile outFile;

    isOk = outFile.Create(tmpPath, true)
      && outFile.Write(outData.data(), outData.size()) == outData.size();
    outFile.Close();
    isOk = isOk && wxRenameFile(tmpPath, outPath, true);
    if (!isOk && wxFileExists(tmpPath))
      wxRemoveFile(tmpPath);
  }
  return isOk;
}
