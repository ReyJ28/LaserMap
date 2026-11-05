LaserMap for Windows
====================

Install (once)
  1. Close Resolume.
  2. Double-click Install-LaserMap.bat   (or right-click Install-LaserMap.ps1 > Run with PowerShell).
     Needs: Windows 64-bit, Resolume Arena 7 (started once), and BEYOND on the same PC for laser output.
     BEYOND must be installed; if it is running during install, BEYONDIO.dll is found automatically.
     Or copy Pangolin's BEYONDIO.dll into this folder before installing.
  To remove: double-click Uninstall-LaserMap.bat.

Use
  1. Start BEYOND. Keep its laser output OFF and the zone's power LOW while you test.
  2. Start Resolume. Put the LaserMap effect on a clip that is playing.
  3. In the effect's Output group tick  Send To BEYOND,  pick the Zone,  tick Armed.
     The LaserMap helper starts by itself. Its label shows "Bridge: connected".
  4. Turn on the Grid mode, and in BEYOND (Settings > Projection Zones) warp that zone until the grid
     sits on your picture. Then switch to any other mode.

Timed modes
  Schedule group: tick Use Schedule and type e.g.  0-8: Trace Clip; 8-16: Beam Runners + Sweep; 1:00-1:20: Grid
  Restart Schedule resets the clock to 0 (press it when the clip starts).

Safety
  LaserMap never arms, disarms or blacks out BEYOND. Arming and limits stay in BEYOND.

If nothing appears
  Open  Documents\LaserMap\lasermap.log  and send it to whoever supports you.
  The helper can also be run by hand:  %LOCALAPPDATA%\LaserMap\lasermap_bridge.exe --test-pattern --zone 1
  (it draws a grid and a moving circle in zone 1 without Resolume).

Pangolin's BEYONDIO.dll is not part of this package.
