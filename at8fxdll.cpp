//
// FX A7
//	Atari 8-bit protected image file (FX7) DLL
//  Windows DLL module
//
#include "pch.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <stdio.h>

// #include "resource.h"

#include "osUtils.h"

#define EXPORT_DLL
extern bool simulInit( void);


HINSTANCE hInstance;

#if 0
#define DEVELOPER_BUILD
#endif

#define DLL_VERS_TXT "0.3d"

//#ifdef DEVELOPER_BUILD
//#undef DLL_VERS_TXT 
//#define DLL_VERS_TXT "0.3b" "_1Dev"
//#endif


#ifdef INHOUSE

void myDebug( const char *msg, ...)
{
	va_list args;
	HANDLE hStdout;
	char buf[ 256];		// enough !!! ???

	AllocConsole();
	hStdout = GetStdHandle( STD_OUTPUT_HANDLE);

	va_start( args, msg);

	vsprintf( buf, msg, args);
	strcat( buf, "\n");


	DWORD dummy;
	WriteConsole( hStdout, buf, strlen( buf), &dummy, &dummy);
}

#endif

//
//
//

#if 0
static BOOL CALLBACK configDlgProc( HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	unsigned type;
	eDRIVETYPE drvType;

	switch (uMsg)
	{
	case WM_INITDIALOG:
		SetWindowText( GetDlgItem( hDlg, IDC_VERSION), DLL_VERS_TXT);

		SendDlgItemMessage( hDlg, IDC_DRVTYPE, CB_ADDSTRING, 0, (LPARAM) "1050");
		SendDlgItemMessage( hDlg, IDC_DRVTYPE, CB_ADDSTRING, 0, (LPARAM) "810");

		switch( getDrvType( 0))
		{
		case eDRV_1050DD:
			type = 0;
			break;
		case eDRV_810:
			type = 1;
			break;
		}

		SendDlgItemMessage( hDlg, IDC_DRVTYPE, CB_SETCURSEL, type, 0);
		return TRUE;

	case WM_COMMAND:

		switch( LOWORD(wParam))
		{
		case IDOK:
			type = SendDlgItemMessage( hDlg, IDC_DRVTYPE, CB_GETCURSEL, 0, 0);
			switch( type)
			{
			case 0:
				drvType = eDRV_1050DD;
				break;
			case 1:
				drvType = eDRV_810;
				break;
			default:
				goto invalid;
			}
			setDrvType( drvType, 0);
invalid:
			EndDialog( hDlg, TRUE);
			return TRUE;

		case IDCANCEL:
			EndDialog( hDlg, FALSE);
			return TRUE;
		}
	}
	return FALSE;
}
#endif


BOOL vapsioDlgConfig( HWND hWnd, unsigned flags, void *)
{
#if 0
	DialogBox( hInstance, MAKEINTRESOURCE( IDD_CONFIG), hWnd, configDlgProc);
#endif
	return TRUE;
}

//
//
//

static void betaBanner( void)
{
	// Change for propper version at compile time !!!
	static const char betaMsg[] =
		"Vapi DLL version " DLL_VERS_TXT "\n\n"
		"Beta version. Copyright (c) 2004-06 by Jorge Cwik.";

	MessageBox( NULL, (LPCWSTR) betaMsg, (LPCWSTR) "Warning", MB_OK | MB_ICONWARNING);
}

BOOL WINAPI DllMain(
		HINSTANCE hinstDLL,  // handle to DLL module
		DWORD fdwReason,     // reason for calling function
		LPVOID lpReserved )  // reserved
{

	// Perform actions based on the reason for calling.
	switch( fdwReason ) 
	{
	case DLL_PROCESS_ATTACH:
		// Initialize once for each new process.
		// Return FALSE to fail DLL load.
		hInstance = hinstDLL;

	#ifndef DEVELOPER_BUILD
		betaBanner();
	#endif

		if( !simulInit())
			return FALSE;
		break;

	case DLL_THREAD_ATTACH:
		// Do thread-specific initialization.
		break;

	case DLL_THREAD_DETACH:
		// Do thread-specific cleanup.
		break;

	case DLL_PROCESS_DETACH:
		// Perform any necessary cleanup.
		break;
	}

	return TRUE;  // Successful DLL_PROCESS_ATTACH.
}
 