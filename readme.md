# Introduction
VMware-Helper is a command line tool to start, stop, suspend virtual images using VMware Player. It is similar to vmware-run tool provided by VMware but does not require [VMware VIX SDK](https://www.vmware.com/support/developer/vix-api/) to run, which comes as a 150MB installer. VMware-Helper is 14MB (under 6MB compressed), including the redistributable VMware DLLs. 

# Usage

```
Usage: vmware-helper.exe <command> <vmxpath> [options]

  <command>
    the desired action, either `-start`, `-suspend`, `-stop`, or `-status`

  <vmxpath>
    absolute path to the .vmx file for the virtual machine

  [options]
      -nogui: start virtual machine without UI
      -help: shows this help

Examples:
  vmware-helper.exe -start C:\Users\Name\VirtualMachine.vmx
  vmware-helper.exe -stop "C:\Users\User Name\Virtual Machine.vmx"
```

# Additional Files
To obtain the required files install VMware VIX SDK. Copy the following files from the VIX installation to your VMware-Helper directory. Once these are copied you can compile, link and run VMware-Helper without VIX, so feel free to uninstall it if you wish.

**Copy to `src\vmware`**
- vix.h
- vix.lib
- vm_basic_types.h


**Copy to `debug` or `release` folder**
- glib-2.0.dll
- gobject-2.0.dll
- gvmomi-vix-1.13.2.dll
- iconv.dll
- intl.dll
- libcurl.dll
- libeay32.dll
- liblber.dll
- libldap_r.dll
- libxml2.dll
- ssleay32.dll
- vix.dll

Note: For running against VMware Player 6, use the DLL files from the `Workstation-10.0.0-and-vSphere-5.5.0\32bit` folder.