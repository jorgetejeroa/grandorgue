/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiMarkers.h"

#include <algorithm>

#include <wx/filename.h>
#include <wx/textfile.h>

wxString GOMidiMarkers::getMarkersPath(const wxString &midiPath) {
  wxFileName fileName(midiPath);

  fileName.SetExt(wxT("markers"));
  return fileName.GetFullPath();
}

void GOMidiMarkers::Sort() {
  std::stable_sort(
    m_markers.begin(), m_markers.end(), [](const Marker &a, const Marker &b) {
      return a.m_position < b.m_position;
    });
}

void GOMidiMarkers::Load(const wxString &midiPath) {
  const wxString path = getMarkersPath(midiPath);
  wxTextFile file;

  m_markers.clear();
  if (wxFileExists(path) && file.Open(path, wxConvUTF8)) {
    for (size_t n = file.GetLineCount(), i = 0; i < n; i++) {
      const wxString &line = file[i];
      const int tab = line.Find(wxT('\t'));
      double position;

      if (tab > 0 && line.Left(tab).ToCDouble(&position) && position >= 0)
        m_markers.push_back({position, line.Mid(tab + 1).Trim()});
    }
    file.Close();
    Sort();
  }
}

bool GOMidiMarkers::Save(const wxString &midiPath) const {
  const wxString path = getMarkersPath(midiPath);
  bool isOk = true;

  if (m_markers.empty()) {
    if (wxFileExists(path))
      isOk = wxRemoveFile(path);
  } else {
    wxTextFile file;

    isOk = wxFileExists(path) ? file.Open(path, wxConvUTF8) : file.Create(path);
    if (isOk) {
      file.Clear();
      for (const Marker &marker : m_markers)
        file.AddLine(
          wxString::FromCDouble(marker.m_position, 0) + wxT("\t")
          + marker.m_name);
      isOk = file.Write(wxTextFileType_None, wxConvUTF8);
      file.Close();
    }
  }
  return isOk;
}

unsigned GOMidiMarkers::Add(double position, const wxString &name) {
  unsigned index = 0;

  while (index < m_markers.size() && m_markers[index].m_position <= position)
    index++;
  m_markers.insert(m_markers.begin() + index, {position, name});
  return index;
}

void GOMidiMarkers::Remove(unsigned index) {
  if (index < m_markers.size())
    m_markers.erase(m_markers.begin() + index);
}

void GOMidiMarkers::Rename(unsigned index, const wxString &name) {
  if (index < m_markers.size())
    m_markers[index].m_name = name;
}

unsigned GOMidiMarkers::Move(unsigned index, double deltaMs) {
  unsigned newIndex = index;

  if (index < m_markers.size()) {
    Marker marker = m_markers[index];

    marker.m_position = std::max(0.0, marker.m_position + deltaMs);
    m_markers.erase(m_markers.begin() + index);
    newIndex = Add(marker.m_position, marker.m_name);
  }
  return newIndex;
}

void GOMidiMarkers::Scale(double timeFactor) {
  for (Marker &marker : m_markers)
    marker.m_position *= timeFactor;
}
