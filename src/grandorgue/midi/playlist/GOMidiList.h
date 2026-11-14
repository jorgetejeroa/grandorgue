/*
 * Copyright 2006 Milan Digital Audio LLC
 * Copyright 2009-2026 GrandOrgue contributors (see AUTHORS)
 * License GPL-2.0 or later
 * (https://www.gnu.org/licenses/old-licenses/gpl-2.0.html).
 */

#ifndef GOMIDILIST_H
#define GOMIDILIST_H

#include <vector>

#include <wx/arrstr.h>
#include <wx/string.h>

/**
 * A prepared list of pieces, for example for one celebration. The lists of a
 * playlist folder are stored in its subfolder "Lists" (translated), one UTF-8
 * text file per list with the extension ".golist":
 *   type=mass|free
 *   <moment><tab><MIDI file name relative to the playlist folder>
 * A list of type "mass" shows the liturgical moment of each piece.
 */
class GOMidiList {
public:
  struct Item {
    wxString m_moment; // empty in a free list
    wxString m_FileName;
  };

private:
  bool m_IsMass;
  std::vector<Item> m_items;

public:
  GOMidiList() : m_IsMass(false) {}

  /** @return the folder of the lists of a playlist folder */
  static wxString getListsDir(const wxString &playlistDir);
  /** @return the file of a list with the given name */
  static wxString getListPath(
    const wxString &playlistDir, const wxString &name);
  /** @return the name of a list from its file */
  static wxString getListName(const wxString &listPath);
  /**
   * @return the files of all lists of a playlist folder, the most recently
   *   changed first
   */
  static wxArrayString getListPaths(const wxString &playlistDir);
  /** @return the liturgical moments of a mass, in their order */
  static const wxArrayString &getMassMoments();
  /** @return whether a moment of a mass is optional */
  static bool isOptionalMoment(unsigned momentIndex);
  /**
   * Proposes the moment of a piece added after another one: the next
   * moment that is not optional
   * @param previousMoment the moment of the previous piece or empty
   */
  static wxString proposeNextMoment(const wxString &previousMoment);
  /** @return a name without the characters not allowed in file names */
  static wxString cleanName(const wxString &name);

  bool Load(const wxString &listPath);
  bool Save(const wxString &listPath) const;

  bool IsMass() const { return m_IsMass; }
  void SetMass(bool isMass) { m_IsMass = isMass; }
  std::vector<Item> &GetItems() { return m_items; }
  const std::vector<Item> &GetItems() const { return m_items; }
};

#endif
