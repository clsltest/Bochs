# Build System Fix for fw_cfg Device

## Problem

After cherry-picking the UEFI/OVMF support commit (2d88167) to a release-2.6.11 branch, the build fails with unresolved symbols for the fw_cfg plugin:

**Linux/Unix:**
```
undefined reference to `libfwcfg_plugin_entry'
```

**Windows/MSVC:**
```
libiodev.a(devices.o) error LNK2019: unresolved external symbol libfwcfg_plugin)entry
referenced in function bx_devices_c::init
```

These errors indicate that the fw_cfg device source files are not being compiled and linked into the build.

## Root Cause

The original commit (2d88167) included all the C++ source code changes and updated the Unix Makefile dependency rules (Makefile.in), but:

1. **Did NOT add `fwcfg.o` to the PCI_OBJS list** in configure/configure.ac
2. **Did NOT update the Visual Studio project files** (.vcxproj) for Windows builds

The Makefile had the build *rules* for fwcfg.o but it wasn't actually in the list of objects to compile.

## Solution

### Unix/Linux Build Fix

**Modified files:**
- `bochs/configure` - Added `fwcfg.o` to PCI_OBJS list
- `bochs/configure.ac` - Added `fwcfg.o` to PCI_OBJS list (source file for configure)

**Change:**
```bash
# Before:
PCI_OBJS='pci.o pci2isa.o pci_ide.o acpi.o hpet.o'

# After:
PCI_OBJS='pci.o pci2isa.o pci_ide.o acpi.o hpet.o fwcfg.o'
```

This ensures that fwcfg.o is compiled when PCI support is enabled (which is required for UEFI/OVMF).

**Build instructions after fix:**
```bash
cd bochs
./configure --enable-pci  # (plus any other options you need)
make
```

### Windows/MSVC Build Fix

The following Visual Studio project files have been updated:

### 1. Non-Plugin Build (vs2019/iodev.vcxproj)

**Added source file:**
```xml
<ClCompile Include="..\iodev\fwcfg.cc" />
```

**Added header file:**
```xml
<ClInclude Include="..\iodev\fwcfg.h" />
```

These additions allow the fw_cfg device to be compiled into the static iodev library when building in non-plugin mode.

### 2. Plugin Build (vs2019-plugins/bx_fwcfg.vcxproj)

**Created new project file:** `bx_fwcfg.vcxproj`

This file was created by copying the template from `bx_hpet.vcxproj` and modifying it for fwcfg:
- Changed project GUID to: `{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}`
- Changed output directories to: `bx_fwcfg`
- Changed source files to: `fwcfg.cc` and `fwcfg.h`

This allows the fw_cfg device to be built as a loadable plugin (DLL) in plugin mode.

### 3. Solution File (vs2019-plugins/bochs-plugins.sln)

**Added project entry:**
```
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "bx_fwcfg", "bx_fwcfg.vcxproj", "{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}"
EndProject
```

**Added configuration entries:**
```
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Debug|Win32.ActiveCfg = Debug|Win32
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Debug|Win32.Build.0 = Debug|Win32
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Debug|x64.ActiveCfg = Debug|x64
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Debug|x64.Build.0 = Debug|x64
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Release|Win32.ActiveCfg = Release|Win32
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Release|Win32.Build.0 = Release|Win32
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Release|x64.ActiveCfg = Release|x64
{8F3C91A2-7D4E-4B5C-9A1F-E6D8C2F1B4A7}.Release|x64.Build.0 = Release|x64
```

This registers the bx_fwcfg project with the solution so Visual Studio knows to build it.

## Files Modified

**Unix/Linux:**
1. `bochs/configure` - Added fwcfg.o to PCI_OBJS
2. `bochs/configure.ac` - Added fwcfg.o to PCI_OBJS (source)

**Windows/MSVC:**
3. `bochs/build/win32/vs2019-workspace/vs2019/iodev.vcxproj` - Added fwcfg.cc and fwcfg.h
4. `bochs/build/win32/vs2019-workspace/vs2019-plugins/bx_fwcfg.vcxproj` - Created new plugin project
5. `bochs/build/win32/vs2019-workspace/vs2019-plugins/bochs-plugins.sln` - Registered bx_fwcfg project

## Build Instructions

After applying these changes:

1. Open `bochs/build/win32/vs2019-workspace/vs2019/bochs.sln` (for non-plugin build)
   OR
2. Open `bochs/build/win32/vs2019-workspace/vs2019-plugins/bochs-plugins.sln` (for plugin build)

3. Select your configuration (Debug/Release, Win32/x64)

4. Build the solution

The fw_cfg device will now be properly compiled and linked, resolving the LNK2019 error.

## Compatibility

These project files are for Visual Studio 2019 (v142 toolset). If you're using a different version of Visual Studio:

- **VS2017**: The v142 toolset should work, or you can change `<PlatformToolset>v142</PlatformToolset>` to `v141`
- **VS2022**: The v142 toolset should work, or you can upgrade the projects via VS2022's migration tool
- **Older versions**: You may need to regenerate the project files from the Makefile.in

## Note for Other Branches

If you cherry-pick the UEFI/OVMF commit (2d88167) to other branches or forks, you'll need to apply these MSVC build system updates as well, or the Windows build will fail.
