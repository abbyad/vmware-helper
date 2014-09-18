/* *********************************************************************
 * Copyright (c) 2014 Marc Abbyad
 * *********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include <crtdbg.h>  // For _CrtSetReportMode
#include <malloc.h>

#include "vmware\vix.h"

#define POLLINTERVAL 1000	// milliseconds
#define STARTUPWAIT 0	// milliseconds

// Exit codes
#define STATUS_ON	10
#define STATUS_OFF	11

// #define DEBUG

/*
 * Certain arguments differ when using VIX with VMware Server 2.0
 * and VMware Workstation.
 *
 * Comment out this definition to use this code with VMware Server 2.0.
 */
#define USE_WORKSTATION

#ifdef USE_WORKSTATION

#define  CONNTYPE    VIX_SERVICEPROVIDER_VMWARE_PLAYER	// Set the VMware product type

#define  HOSTNAME ""
#define  HOSTPORT 0
#define  USERNAME ""
#define  PASSWORD ""

#define VMXPATH_INFO "absolute path to the .vmx file for the virtual machine"

#else    // USE_WORKSTATION

/*
 * For VMware Server 2.0
 */

#define CONNTYPE VIX_SERVICEPROVIDER_VMWARE_VI_SERVER

#define HOSTNAME "https://192.2.3.4:8333/sdk"
/*
 * NOTE: HOSTPORT is ignored, so the port should be specified as part
 * of the URL.
 */
#define HOSTPORT 0
#define USERNAME "root"
#define PASSWORD "hideme"

#define  VMPOWEROPTIONS VIX_VMPOWEROP_NORMAL

#define VMXPATH_INFO "datastore-relative path to the .vmx file for the virtual machine, such as \"[standard] ubuntu/ubuntu.vmx\""

#endif    // USE_WORKSTATION


/*
 * Global variables.
 */

static char *progName;
VixError err;
char *vmxPath;
char *command;
VixToolsState powerState = 0;
VixVMPowerOpOptions powerOptions = VIX_VMPOWEROP_LAUNCH_GUI;	// By default launches the UI when powering on the virtual machine.
Bool wait = FALSE;
Bool heartbeat = FALSE;
FILE *fHeartbeat = NULL;
char *strIPAddress = "";
char *strHeartbeat;
char *strVars = "";
char strVarsTmp[256];
int fErr;

char *token;
char *next_token;

VixHandle hostHandle = VIX_INVALID_HANDLE;
VixHandle jobHandle = VIX_INVALID_HANDLE;
VixHandle vmHandle = VIX_INVALID_HANDLE;

char sTime[26];
struct tm sTm;
__int64 lTime;


/*
 * Local functions.
 */

////////////////////////////////////////////////////////////////////////////////
static void
usage()
{
   fprintf(stdout, "\n\
Usage: %s <command> <vmxpath> [options]\n\
\n\
  <command>\n\
    desired action, either `-start`, `-suspend`, `-stop`, `-status` or `-getip`\n\
  \n\
  <vmxpath>\n\
    %s\n\
  \n\
  [options]\n\
      -nogui: start virtual machine without UI\n\
      -wait: after starting wait for virtual machine to exit\n\
      -help: shows this help\n\
      -heartbeat: maintains ini file `heartbeat.log` with status information\n\
      -vars \"space separated variables\": guestinfo values for heartbeat\n\
\n\
Examples:\n\
  %s -start C:\\Users\\Name\\VirtualMachine.vmx\n\
  %s -start vm.vmx -heartbeat -vars \"ip modem_status\"\n\
  %s -stop \"C:\\Users\\User Name\\Virtual Machine.vmx\"\n\n\
", progName, VMXPATH_INFO, progName, progName, progName);
}


static char* getTime(){
	time(&lTime);

	if (localtime_s(&sTm, &lTime)) {
		// error in conversion
		sprintf_s(sTime, sizeof(sTime), "");
	}
	else { 
		strftime(sTime, sizeof(sTime), "%Y-%m-%d %H:%M:%S", &sTm);
	}
	return sTime;
}

void myInvalidParameterHandler(const wchar_t* expression,
	const wchar_t* function,
	const wchar_t* file,
	unsigned int line,
	uintptr_t pReserved)
{
	wprintf(L"Invalid parameter detected in function %s.\n", function);
	wprintf(L" File: %s Line: %d\n", file, line);
	wprintf(L"Expression: %s\n", expression);
}

static void doHeartbeat(char *msg) {
	if (heartbeat) {
		fErr = fopen_s(&fHeartbeat, "heartbeat.log", "w");
		if (fErr)
		{
			fprintf(stderr, "[%s] Failed to open heartbeat file for writing [%d] \n", getTime(), fErr);
		}
		else {
#ifdef DEBUG
			fprintf(stdout, "[%s] Opened heartbeat file for writing\n", getTime());
#endif
			fprintf(fHeartbeat, "date = %s\n", getTime());
			fprintf(fHeartbeat, "%s", msg);
		}

		// close regardless of whether it was opened or not
		if (fHeartbeat) {
			fErr = fclose(fHeartbeat);
			if (fErr == 0)
			{
#ifdef DEBUG 
				fprintf(stdout, "[%s] The heartbeat file was closed\n", getTime());
#endif
			}
			else
			{
				fprintf(stderr, "[%s] Failed to close the heartbeat file [%d] \n", getTime(), fErr);
			}
		}
	}
}

static void exitMain(int code) {
	if (fHeartbeat) {
		fErr = fclose(fHeartbeat);
		if (fErr == 0)
		{
#ifdef DEBUG 
			fprintf(stdout, "[%s] The heartbeat file was closed\n", getTime());
#endif
		}
		else
		{
			fprintf(stderr, "[%s] Failed to close the heartbeat file [%d] \n", getTime(), fErr);
		}
	}
	
	free(strHeartbeat);

	exit(code);
}

static void errorAbort() {
#ifdef DEBUG
	fprintf(stderr, "[%s] ABORTED\n", getTime());
#endif
	Vix_ReleaseHandle(jobHandle);
	Vix_ReleaseHandle(vmHandle);

	VixHost_Disconnect(hostHandle);
	exitMain(EXIT_FAILURE);
}

static void vmConnectOpen()
{
	jobHandle = VixHost_Connect(VIX_API_VERSION,
		CONNTYPE,
		HOSTNAME, // *hostName,
		HOSTPORT, // hostPort,
		USERNAME, // *userName,
		PASSWORD, // *password,
		0, // options,
		VIX_INVALID_HANDLE, // propertyListHandle,
		NULL, // *callbackProc,
		NULL); // *clientData);
	err = VixJob_Wait(jobHandle,
		VIX_PROPERTY_JOB_RESULT_HANDLE,
		&hostHandle,
		VIX_PROPERTY_NONE);
	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] Failed to connect to host [%d]\n", getTime(), VIX_ERROR_CODE(err));
		errorAbort();
	}
	Vix_ReleaseHandle(jobHandle);
	
	jobHandle = VixVM_Open(hostHandle,
		vmxPath,
		NULL, // VixEventProc *callbackProc,
		NULL); // void *clientData);
	err = VixJob_Wait(jobHandle,
		VIX_PROPERTY_JOB_RESULT_HANDLE,
		&vmHandle,
		VIX_PROPERTY_NONE);
	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] Failed to open virtual machine [%d]\n", getTime(), VIX_ERROR_CODE(err));
		errorAbort();
	}
	Vix_ReleaseHandle(jobHandle);
	jobHandle = VIX_INVALID_HANDLE;
}

static void vmGetStatus()
{
	// Should test if already powered
	err = Vix_GetProperties(vmHandle,
		VIX_PROPERTY_VM_POWER_STATE,
		&powerState,
		VIX_PROPERTY_NONE);
	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] Failed to get virtual machine status [%d]\n", getTime(), VIX_ERROR_CODE(err));
		errorAbort();
	}
}

static char* vmGetGuestVars(char* keyName)
{
	char* keyValue;
	keyValue = "";
	// Wait until guest is completely booted.
	jobHandle = VixVM_WaitForToolsInGuest(vmHandle,
		10, // timeoutInSeconds
		NULL, // callbackProc
		NULL); // clientData

	err = VixJob_Wait(jobHandle, VIX_PROPERTY_NONE);
	Vix_ReleaseHandle(jobHandle);

	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] VM not yet loaded [%d]\n", getTime(), VIX_ERROR_CODE(err));
		return "";
	}

	jobHandle = VixVM_ReadVariable(vmHandle,
		VIX_VM_GUEST_VARIABLE,
		keyName,
		0, // options
		NULL, // callbackProc
		NULL); // clientData);
	err = VixJob_Wait(jobHandle,
		VIX_PROPERTY_JOB_RESULT_VM_VARIABLE_STRING,
		&keyValue,
		VIX_PROPERTY_NONE);

	Vix_ReleaseHandle(jobHandle);

	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] Failed to get guest variable `%s` [%d]\n", getTime(), keyName, VIX_ERROR_CODE(err));
		return "";
	}
	return keyValue;
}

static void vmGetIP()
{
	strIPAddress = vmGetGuestVars("ip");
}

static void printIPAddress(){
	vmGetIP();
	if (strcmp(strIPAddress, "") == 0) {
		fprintf(stdout, "[%s] Could not obtain IP Address\n", getTime());
	}
	else {
		fprintf(stdout, "[%s] IP Address: %s\n", getTime(), strIPAddress);
	}
}

static void vmStatus()
{
#ifdef DEBUG
	fprintf(stderr, "[%s] Checking status for \"%s\"\n", getTime(), vmxPath);
#endif
	vmGetStatus();
	if (VIX_POWERSTATE_POWERED_ON & powerState) {
		fprintf(stdout, "[%s] Virtual machine is powered on\n", getTime());
		printIPAddress();
		exitMain(STATUS_ON);
	}
	else {
		fprintf(stdout, "[%s] Virtual machine is powered off\n", getTime());
		exitMain(STATUS_OFF);
	}
	// not releasing job handle?
}

static void appendHeartbeat(char * str, int length)
{
	if ((strHeartbeat = realloc(strHeartbeat, _msize(strHeartbeat) + length)) != NULL)
	{
		strncat_s(strHeartbeat, _msize(strHeartbeat), str, length);
	}
	else {		// error
		fprintf(stderr, "[%s] Failed to allocate additional %d bytes (total %d) to prepare hearbeat file\n", getTime(), length, _msize(strHeartbeat) + length, token);
		exitMain(EXIT_FAILURE);
	}
}

static void vmStart()
{
	vmGetStatus();

	if (VIX_POWERSTATE_POWERED_ON & powerState) {
		// virtual machine is powered on
		fprintf(stderr, "[%s] Virtual machine already running\n", getTime());
	}
	else {
		jobHandle = VixVM_PowerOn(vmHandle,
			powerOptions,
			VIX_INVALID_HANDLE,
			NULL, // *callbackProc,
			NULL); // *clientData);
		err = VixJob_Wait(jobHandle, VIX_PROPERTY_NONE);
		if (VIX_FAILED(err)) {
			fprintf(stderr, "[%s] Failed to start virtual machine [%d]\n", getTime(), VIX_ERROR_CODE(err));
			errorAbort();
		}
		else {
			fprintf(stdout, "[%s] Virtual machine started\n", getTime());
		}
	}

	if (wait)
	{
		Sleep(STARTUPWAIT);
		do {
			
			strHeartbeat = malloc(2);
			strcpy_s(strHeartbeat, _countof(strHeartbeat), "\0");

			// Test the power state.
			vmGetStatus();
			if (VIX_POWERSTATE_POWERED_ON & powerState) {
				// virtual machine is powered on
#ifdef DEBUG
				fprintf(stdout, "[%s] Virtual machine running\n", getTime());
#endif
				if (heartbeat) {

					char* s = "vm = RUNNING\n";
					appendHeartbeat(s, _scprintf("%s", s));

					// get guest vars, starting with first token
					strcpy_s(strVarsTmp, _countof(strVarsTmp), strVars);
					token = strtok_s(strVarsTmp, " ", &next_token);

					// walk through other tokens
					while (token != NULL)
					{
						char* v = vmGetGuestVars(token);
						int c = _scprintf("%s = %s\n", token, v) + 1;

						if ((s = malloc(c)) != NULL)
						{
							sprintf_s(s, _msize(s), "%s = %s\n", token, v);
							appendHeartbeat(s, _msize(s));
						}
						else { // error
							fprintf(stderr, "[%s] Failed to allocate %d bytes for the token `%s`\n", getTime(), c, token);
							exitMain(EXIT_FAILURE);
						}
						free(s);

						token = strtok_s(NULL, " ", &next_token);
					}
					
					doHeartbeat(strHeartbeat);
				}
			}
			else if (VIX_POWERSTATE_POWERED_OFF & powerState) {
				// virtual machine is powered off
				if (heartbeat) {
					doHeartbeat("vm = STOPPED\n");
				}
				break;
			}
			else {
				// only checked for Power State, so will never get here
				if (heartbeat) {
					doHeartbeat("vm = TRANSITION\n");
				}
				fprintf(stderr, "[%s] Virtual machine in transition state [%d]\n", getTime(), powerState);
			}
			Sleep(POLLINTERVAL);
		} while (VIX_POWERSTATE_POWERED_ON & powerState);
	}
	Vix_ReleaseHandle(jobHandle);
	jobHandle = VIX_INVALID_HANDLE;
}

static void vmStop()
{
	jobHandle = VixVM_PowerOff(vmHandle,
		VIX_VMPOWEROP_NORMAL,
		NULL, // *callbackProc,
		NULL); // *clientData);
	err = VixJob_Wait(jobHandle, VIX_PROPERTY_NONE);
	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] Failed to stop virtual machine, may have already been stopped [%d]\n", getTime(), VIX_ERROR_CODE(err));
		errorAbort();
	}
	else {
		fprintf(stdout, "[%s] Stopped virtual machine [%d]\n", getTime(), VIX_ERROR_CODE(err));
	}
	Vix_ReleaseHandle(jobHandle);
	jobHandle = VIX_INVALID_HANDLE;
}

static void vmSuspend(){
	jobHandle = VixVM_Suspend(vmHandle,
		VIX_VMPOWEROP_NORMAL,
		NULL, // *callbackProc,
		NULL); // *clientData);
	err = VixJob_Wait(jobHandle, VIX_PROPERTY_NONE);
	if (VIX_FAILED(err)) {
		fprintf(stderr, "[%s] Failed to suspend virtual machine, may have already been stopped [%d]\n", getTime(), VIX_ERROR_CODE(err));
		errorAbort();
	}
	else {
		fprintf(stdout, "[%s] Suspended virtual machine [%d]\n", getTime(), VIX_ERROR_CODE(err));
	}
	Vix_ReleaseHandle(jobHandle);
	jobHandle = VIX_INVALID_HANDLE;
}

////////////////////////////////////////////////////////////////////////////////
int
main(int argc, char **argv)
{
	// use our invalid parameter handler for more graceful error handling and prevent crashes
	_invalid_parameter_handler oldHandler, newHandler;
	newHandler = myInvalidParameterHandler;
	oldHandler = _set_invalid_parameter_handler(newHandler);

	// Disable the message box for assertions.
	_CrtSetReportMode(_CRT_ASSERT, 0);

    progName = argv[0];
	if (argc >= 3) {
		command = argv[1];
		vmxPath = argv[2];
    } else {
        usage();
        exitMain(EXIT_FAILURE);
    }

	// check for flags
	for (int i = 1; i < argc; i++){
		if ((strcmp(argv[i], "nogui") == 0) || (strcmp(argv[i], "-nogui") == 0)) {
			powerOptions = VIX_VMPOWEROP_NORMAL;
		}
		if ((strcmp(argv[i], "wait") == 0) || (strcmp(argv[i], "-wait") == 0)) {
			wait = TRUE;
		}
		if ((strcmp(argv[i], "heartbeat") == 0) || (strcmp(argv[i], "-heartbeat") == 0)) {
			heartbeat = TRUE;
		}
		if ((strcmp(argv[i], "var") == 0) || (strcmp(argv[i], "-var") == 0)) {
			if (argc > i + 1) {
				strVars = argv[i + 1];
/*
				fprintf(stderr, "[%s] Vars: [%s]\n", getTime(), strVars);

				char *token;
				char *next_token;

				// get the first token
				token = strtok_s(strVars, " ", &next_token);

				// walk through other tokens
				while (token != NULL)
				{
					fprintf(stderr, " %s\n", token);

					token = strtok_s(NULL, " ", &next_token);
				}
*/
			}
			else {
				usage();
				exitMain(EXIT_FAILURE);
			}
		}
		if ((strcmp(argv[i], "help") == 0) || (strcmp(argv[i], "-help") == 0) || (strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--h") == 0)) {
			usage();
			exitMain(EXIT_FAILURE);
		}
	}

	vmConnectOpen();
	
	if ((strcmp(command, "start") == 0) || (strcmp(command, "-start") == 0)) {
		vmStart();
	}
	else if ((strcmp(command, "stop") == 0) || (strcmp(command, "-stop") == 0))	{
		vmStop();
	}
	else if ((strcmp(command, "suspend") == 0) || (strcmp(command, "-suspend") == 0)) {
		vmSuspend();
	}
	else if ((strcmp(command, "status") == 0) || (strcmp(command, "-status") == 0)) {
		vmStatus();
	}
	else if ((strcmp(command, "getip") == 0) || (strcmp(command, "-getip") == 0)) {
		printIPAddress();
	}
	else {
		usage();
		exitMain(EXIT_FAILURE);
	}

#ifdef DEBUG 
	fprintf(stdout, "[%s] Finished\n", getTime());
#endif

	exitMain(EXIT_SUCCESS);
}
