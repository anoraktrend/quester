# Quester Release Notes

## Release b7c600244e (Stable Release)

This release brings significant enhancements to Quester, including the integration of ProjectM visualizer and various UI/UX improvements.

### Major Features and Changes

1. **ProjectM Visualizer Integration**: Added support for ProjectM, a music visualizer that creates stunning, dynamic visual effects synchronized with your music.
2. **Presets Management**:
   - Added PMV (ProjectM Visualizer) presets
   - Fixed preset installation
3. **Library Management**:
   - Major refactor: Now using MBIDs (MusicBrainz Identifiers) when available for more accurate library management
4. **Queue Management**:
   - Added Queue View functionality
   - Fixed queue option not appearing outside of visualizer
5. **Sorting Options**: Added sort options to improve library navigation
6. **UI/UX Improvements**:
   - Various UI fixes and enhancements
   - Improved progress bar background to match screenshots
   - Tidy up and robustness improvements

### Technical Changes

1. **CMake Configuration**: Updated CMakeLists.txt for better build management
2. **MPRIS Integration**: Changed from MprisClient to DBus implementation
3. **Cache Path Consolidation**: Improved cache management
4. **Code Quality**: Fixed build errors and warnings
5. **Project Structure**: Updated project configuration files

### Known Issues

No major known issues at the time of release.

---

**Start Commit**: dc4e198bf9  
**End Commit**: b7c600244e  
**Release Date**: February 7, 2026