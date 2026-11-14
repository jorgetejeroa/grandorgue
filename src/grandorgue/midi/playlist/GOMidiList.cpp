/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#include "GOMidiList.h"

#include <algorithm>
#include <utility>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/textfile.h>

static const wxString LIST_EXT = wxT("golist");

// whether each moment of getMassMoments() is optional
static const bool OPTIONAL_MOMENTS[] = {
  false, // entrance
  true,  // Kyrie
  true,  // Gloria
  true,  // psalm
  false, // Alleluia
  false, // offertory
  false, // Sanctus
  true,  // Our Father
  true,  // Agnus Dei
  false, // communion
  true,  // thanksgiving
  false, // recessional
};

wxString GOMidiList::getListsDir(const wxString &playlistDir) {
  return playlistDir + wxFileName::GetPathSeparator() + _("Lists");
}

wxString GOMidiList::getListPath(
  const wxString &playlistDir, const wxString &name) {
  return getListsDir(playlistDir) + wxFileName::GetPathSeparator() + name
    + wxT(".") + LIST_EXT;
}

wxString GOMidiList::getListName(const wxString &listPath) {
  return wxFileName(listPath).GetName();
}

wxArrayString GOMidiList::getListPaths(const wxString &playlistDir) {
  const wxString dir = getListsDir(playlistDir);
  wxArrayString paths;
  std::vector<std::pair<time_t, wxString>> dated;
  wxArrayString result;

  if (wxDir::Exists(dir))
    wxDir::GetAllFiles(dir, &paths, wxT("*.") + LIST_EXT, wxDIR_FILES);
  for (const wxString &path : paths)
    dated.push_back({wxFileName(path).GetModificationTime().GetTicks(), path});
  std::sort(dated.begin(), dated.end(), [](const auto &a, const auto &b) {
    return a.first != b.first ? a.first > b.first
                              : a.second.CmpNoCase(b.second) < 0;
  });
  for (const auto &entry : dated)
    result.Add(entry.second);
  return result;
}

const wxArrayString &GOMidiList::getMassMoments() {
  static wxArrayString moments;

  if (moments.IsEmpty()) {
    moments.Add(_("Entrance"));
    moments.Add(_("Kyrie"));
    moments.Add(_("Gloria"));
    moments.Add(_("Psalm"));
    moments.Add(_("Alleluia"));
    moments.Add(_("Offertory"));
    moments.Add(_("Sanctus"));
    moments.Add(_("Our Father"));
    moments.Add(_("Agnus Dei"));
    moments.Add(_("Communion"));
    moments.Add(_("Thanksgiving"));
    moments.Add(_("Recessional"));
  }
  return moments;
}

bool GOMidiList::isOptionalMoment(unsigned momentIndex) {
  return momentIndex < sizeof(OPTIONAL_MOMENTS) / sizeof(OPTIONAL_MOMENTS[0])
    && OPTIONAL_MOMENTS[momentIndex];
}

wxString GOMidiList::proposeNextMoment(const wxString &previousMoment) {
  const wxArrayString &moments = getMassMoments();
  const int previous
    = previousMoment.IsEmpty() ? -1 : moments.Index(previousMoment);
  wxString result;

  // after an unknown moment nothing is proposed
  if (previousMoment.IsEmpty() || previous != wxNOT_FOUND) {
    unsigned next = (unsigned)(previous + 1);

    while (next < moments.GetCount() && isOptionalMoment(next))
      next++;
    if (next < moments.GetCount())
      result = moments[next];
  }
  return result;
}

wxString GOMidiList::cleanName(const wxString &name) {
  wxString result = name;

  for (const wxUniChar c : wxFileName::GetForbiddenChars())
    result.Replace(wxString(c), wxT("-"));
  result.Replace(wxT("/"), wxT("-"));
  result.Replace(wxT("\\"), wxT("-"));
  return result.Trim().Trim(false);
}

bool GOMidiList::Load(const wxString &listPath) {
  wxTextFile file;
  const bool isOk = wxFileExists(listPath) && file.Open(listPath, wxConvUTF8);

  m_IsMass = false;
  m_items.clear();
  if (isOk) {
    for (size_t n = file.GetLineCount(), i = 0; i < n; i++) {
      const wxString &line = file[i];
      const int tab = line.Find(wxT('\t'));

      if (line.StartsWith(wxT("type=")))
        m_IsMass = line.Mid(5).Trim() == wxT("mass");
      else if (tab != wxNOT_FOUND && tab + 1 < (int)line.Length())
        m_items.push_back({line.Left(tab), line.Mid(tab + 1).Trim()});
    }
    file.Close();
  }
  return isOk;
}

bool GOMidiList::Save(const wxString &listPath) const {
  const wxString dir = wxFileName(listPath).GetPath();
  wxTextFile file;
  bool isOk = wxDirExists(dir) || wxMkdir(dir);

  if (isOk)
    isOk = wxFileExists(listPath) ? file.Open(listPath, wxConvUTF8)
                                  : file.Create(listPath);
  if (isOk) {
    file.Clear();
    file.AddLine(m_IsMass ? wxT("type=mass") : wxT("type=free"));
    for (const Item &item : m_items)
      file.AddLine(item.m_moment + wxT("\t") + item.m_FileName);
    isOk = file.Write(wxTextFileType_None, wxConvUTF8);
    file.Close();
  }
  return isOk;
}
