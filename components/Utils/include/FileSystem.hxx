/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2017-2021 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#ifndef FILESYSTEM_HXX_
#define FILESYSTEM_HXX_

namespace esp32cs
{
  
/// Mounts the persistent filesystem.
/// @param cleanup will remove all files from the filesystem during startup.
/// @return true if the filesystem is stored on an SD card, false for SPIFFS.
bool mount_fs(bool cleanup = false);

/// Unmounts the persistent filesystem.
void unmount_fs();

} // namespace esp32cs

#endif // FILESYSTEM_HXX_