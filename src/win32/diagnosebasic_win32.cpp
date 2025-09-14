/*
 * Copyright (C) 2013 Sebastian Herbord. All rights reserved.
 *
 * This file is part of the basic diagnosis plugin for Mod Organizer
 *
 * This plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this plugin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "diagnosebasic.h"

#include <uibase/ifiletree.h>
#include <uibase/imodinterface.h>
#include <uibase/imodlist.h>
#include <uibase/iplugingame.h>
#include <uibase/ipluginlist.h>
#include <uibase/report.h>
#include <uibase/utility.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QtPlugin>

#include <algorithm>
#include <functional>
#include <regex>
#include <vector>

using namespace MOBase;

static bool checkFileAttributes(const QString& path)
{
  WCHAR w_path[32767];
  memset(w_path, 0, sizeof(w_path));
  path.toWCharArray(w_path);

  DWORD attrs = GetFileAttributes(w_path);
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    if (!(attrs & FILE_ATTRIBUTE_ARCHIVE) && !(attrs & FILE_ATTRIBUTE_NORMAL) &&
        (attrs & ~(FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_ARCHIVE))) {
      QString debug;
      debug += QString("%1 ").arg(attrs, 8, 16, QLatin1Char('0'));

      // A C D H I O P R S U V X Z
      debug += (attrs & FILE_ATTRIBUTE_DIRECTORY) ? "D" : " ";
      debug += (attrs & FILE_ATTRIBUTE_ARCHIVE) ? "A" : " ";
      debug += (attrs & FILE_ATTRIBUTE_READONLY) ? "R" : " ";
      debug += (attrs & FILE_ATTRIBUTE_SYSTEM) ? "S" : " ";
      debug += (attrs & FILE_ATTRIBUTE_HIDDEN) ? "H" : " ";
      debug += (attrs & FILE_ATTRIBUTE_OFFLINE) ? "O" : " ";
      debug += (attrs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED) ? "I" : " ";
      debug += (attrs & FILE_ATTRIBUTE_NO_SCRUB_DATA) ? "X" : " ";
      debug += (attrs & FILE_ATTRIBUTE_INTEGRITY_STREAM) ? "V" : " ";
      debug += (attrs & FILE_ATTRIBUTE_PINNED) ? "P" : " ";
      debug += (attrs & FILE_ATTRIBUTE_UNPINNED) ? "U" : " ";
      debug += (attrs & FILE_ATTRIBUTE_COMPRESSED) ? "C" : " ";
      debug += (attrs & FILE_ATTRIBUTE_SPARSE_FILE) ? "Z" : " ";

      debug += QString(" %1").arg(path);

      qDebug() << debug;

      return true;
    }
  } else {
    DWORD error = ::GetLastError();
    qWarning(qUtf8Printable(QString("Unable to get file attributes for %1 (error %2)")
                                .arg(w_path)
                                .arg(error)));
  }
  return false;
}

static bool fixFileAttributes(const QString& path)
{
  bool success = true;

  WCHAR w_path[32767];
  memset(w_path, 0, sizeof(w_path));
  path.toWCharArray(w_path);

  DWORD attrs = GetFileAttributes(w_path);
  if (attrs != INVALID_FILE_ATTRIBUTES) {
    // Clear all the attributes possible, except ARCHIVE, with SetFileAttributes
    if (!SetFileAttributes(
            w_path, attrs & FILE_ATTRIBUTE_ARCHIVE ? FILE_ATTRIBUTE_ARCHIVE : 0)) {
      DWORD error = GetLastError();
      qWarning(qUtf8Printable(QString("Unable to set file attributes for %1 (error %2)")
                                  .arg(path)
                                  .arg(error)));
      success = false;
    }

    // Compression requires DeviceIoControl
    if (attrs & FILE_ATTRIBUTE_COMPRESSED) {
      HANDLE hndl = CreateFile(w_path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
      if (hndl != INVALID_HANDLE_VALUE) {
        USHORT compressionSetting = COMPRESSION_FORMAT_NONE;
        DWORD bytesReturned       = 0;
        if (!DeviceIoControl(hndl, FSCTL_SET_COMPRESSION, &compressionSetting,
                             sizeof(compressionSetting), NULL, 0, &bytesReturned,
                             NULL)) {
          DWORD error = GetLastError();
          qWarning(qUtf8Printable(
              QString("Unable to disable compression for file %1 (error %2)")
                  .arg(path)
                  .arg(error)));
          success = false;
        }
        CloseHandle(hndl);
      } else {
        DWORD error = GetLastError();
        qWarning(qUtf8Printable(
            QString("Unable to open file %1 (error %2)").arg(path).arg(error)));
        success = false;
      }
    }

    // Sparseness requires DeviceIoControl
    if (attrs & FILE_ATTRIBUTE_SPARSE_FILE) {
      HANDLE hndl = CreateFile(w_path, GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
      if (hndl != INVALID_HANDLE_VALUE) {
        FILE_SET_SPARSE_BUFFER setting = {FALSE};
        DWORD bytesReturned            = 0;
        if (!DeviceIoControl(hndl, FSCTL_SET_SPARSE, &setting, sizeof(setting), NULL, 0,
                             &bytesReturned, NULL)) {
          DWORD error = GetLastError();
          qWarning(qUtf8Printable(
              QString("Unable to disable sparseness for file %1 (error %2)")
                  .arg(path)
                  .arg(error)));
          success = false;
        }
        CloseHandle(hndl);
      } else {
        DWORD error = GetLastError();
        qWarning(qUtf8Printable(
            QString("Unable to open file %1 (error %2)").arg(path).arg(error)));
        success = false;
      }
    }

    // As a last ditch effort, set the archive flag
    if (!success) {
      attrs = GetFileAttributes(w_path);
      if (attrs != INVALID_FILE_ATTRIBUTES) {
        if (!SetFileAttributes(w_path, attrs | FILE_ATTRIBUTE_ARCHIVE)) {
          DWORD error = GetLastError();
          qWarning(
              qUtf8Printable(QString("Unable to set file attributes for %1 (error %2)")
                                 .arg(path)
                                 .arg(error)));
        } else {
          success = true;
        }
      } else {
        DWORD error = GetLastError();
        qWarning(
            qUtf8Printable(QString("Unable to get file attributes for %1 (error %2)")
                               .arg(path)
                               .arg(error)));
      }
    }
  } else {
    DWORD error = GetLastError();
    qWarning(qUtf8Printable(QString("Unable to get file attributes for %1 (error %2)")
                                .arg(path)
                                .arg(error)));
    success = false;
  }

  return success;
}
