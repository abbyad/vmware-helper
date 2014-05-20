VMware-Helper is a command line tool to start, stop, suspend virtual images using VMware Player. It is similar to vmware-run tool provided by VMware but does not require [VMware VIX SDK](https://www.vmware.com/support/developer/vix-api/) to run, which comes as a 150MB installer. VMware-Helper is 14MB (under 6MB compressed), including the redistributable VMware DLLs. 

To obtain the required files install VMware VIX SDK. Copy the following files from the VIX installation to your VMware-Helper directory. 

src/vmware:
-----------
vix.h
vix.lib
vm_basic_types.h


debug or release:
-----------
glib-2.0.dll
gobject-2.0.dll
gvmomi-vix-1.13.2.dll
iconv.dll
intl.dll
libcurl.dll
libeay32.dll
liblber.dll
libldap_r.dll
libxml2.dll
ssleay32.dll
vix.dll

Note: For running against VMware Player 6, use the .dll files in "Workstation-10.0.0-and-vSphere-5.5.0\32bit".