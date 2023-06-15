/* Splice
 *
 * (c) 2023 Michael Toulouse
 *
 */

#define WINVER 0x0600
#define _WIN32_WINNT 0x0600
#include "sox.h"
#include "splice.h"
#include <stdlib.h>
#include <errno.h>
#include <windows.h>
#include <shobjidl.h>
#include <objbase.h>
#include <string.h>
#include <strsafe.h>
#include <stdio.h>
#include <assert.h>


static sox_format_t * in, * out;

/**
 * General Utilities
 *
 */
static char const * str_time(double seconds)
{
  static TCHAR string[16][50];
  size_t cchDest = 50;
  static int i;
  LPCTSTR pszFormatWithHours = TEXT("%02i:%02i:%05.2f");
  LPCTSTR pszFormat = TEXT("%02i:%05.2f");
  int hours, mins = seconds / 60;
  seconds -= mins * 60;
  hours = mins / 60;
  mins -= hours * 60;
  i = (i+1) & 15;
  if (hours > 0)
  {
    StringCchPrintf(string[i], cchDest, pszFormatWithHours, hours, mins, seconds);
  } else {
    StringCchPrintf(string[i], cchDest, pszFormat, mins, seconds);
  }
  return string[i];
}

/**
 * SoX-dependent Functions
 *
 */

/* All done; tidy up... */
static int cleanup()
{
  STRSAFE_LPSTR sox_wildcard = "libSoX.tmp*";
  TCHAR szTempFileWildcard[MAX_PATH];
  TCHAR szCurrentTempFileName[MAX_PATH];
  WIN32_FIND_DATA fdFile;
  HANDLE hFind = NULL;

  if (in != NULL) sox_close(in);
  if (out != NULL) sox_close(out);
  sox_quit();
  GetTempPathA(MAX_PATH, szTempFileWildcard);
  StringCbCatA(szTempFileWildcard, MAX_PATH, sox_wildcard);
  if((hFind = FindFirstFile(szTempFileWildcard, &fdFile)) != INVALID_HANDLE_VALUE)
  {
    do
    {
      /* FindFirstFile will always return "." and ".."
       * as the first two directories. */
      if(strcmp(fdFile.cFileName, ".") != 0
        && strcmp(fdFile.cFileName, "..") != 0)
      {
        GetTempPathA(MAX_PATH, szCurrentTempFileName);
        StringCbCatA(szCurrentTempFileName, MAX_PATH, fdFile.cFileName);
        DeleteFileA(szCurrentTempFileName);
      }
    }
    while(FindNextFile(hFind, &fdFile)); /* Find the next file. */
  }
  return 0;
}

void show_name_and_runtime(sox_format_t * in)
{
  double secs;
  uint64_t ws;
  char const * text = NULL;

  ws = in->signal.length / max(in->signal.channels, 1);
  secs = (double)ws / max(in->signal.rate, 1);

  printf_s("FILE: %s: \t\t\t%-15.15s\n", in->filename, str_time(secs));
}

/**
 * Windows functions and variables
 *
 */

HMENU hMenu;
IFileDialog* pFileOpenDialog;
// static const IID IID_IFileDialog = {0x42f85136, 0xdb7e, 0x439c, {0x85, 0xf1, 0xe4, 0x07, 0x5d, 0x13, 0x5f, 0xc8}};

#define IDM_FILE_OPEN 1
#define IDM_FILE_EXIT 3

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#define NOMINMAX // from example on stackoverflow.com
typedef struct StateInfo { 
}StateInfo;

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  const wchar_t CLASS_NAME[] = L"Splicing Audio Files";

  WNDCLASS wc = { };

  wc.lpfnWndProc    = WindowProc;
  wc.hInstance      = hInstance;
  wc.lpszClassName  = CLASS_NAME;

  RegisterClass(&wc);

  struct StateInfo *pState = malloc(sizeof(StateInfo));

  if (pState == NULL)
  {
    return 0;
  }
  // Initialize COM library
  HRESULT hr = CoInitialize(NULL);
  if (FAILED(hr))
  {
      // Handle COM initialization error
      return -1;
  }

  HWND hwnd = CreateWindowEx(
    0,                      // Optional window styles
    CLASS_NAME,             // Window class
    L"Splicing Audio",      // Window text
    WS_OVERLAPPEDWINDOW,    // Window style
    // Size and position
    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

    NULL,                   // Parent window
    hMenu,                  // Menu
    hInstance,              // Instance handle
    pState                  // Additional application data
  );

  if (hwnd == NULL)
  {
    return 0;
  }

  hMenu = CreateMenu();
  HMENU hFileMenu = CreateMenu();

  AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"File");
  AppendMenu(hFileMenu, MF_STRING, IDM_FILE_OPEN, L"Open");
  AppendMenu(hFileMenu, MF_STRING, IDM_FILE_EXIT, L"Exit");

  SetMenu(hwnd, hMenu);
  ShowWindow(hwnd, nCmdShow);
  UpdateWindow(hwnd);
  pFileOpenDialog = NULL;

  // Run the message loop.
  MSG msg = { };
  while(GetMessage(&msg, NULL, 0, 0) > 0)
  {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  // Uninitialize COM library
  CoUninitialize(); 
  return msg.wParam;
}

StateInfo* GetAppState(HWND hwnd)
{
  LONG_PTR ptr = GetWindowLongPtr(hwnd, GWLP_USERDATA);
  StateInfo *pState = (StateInfo*)(ptr);
  return pState;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  StateInfo *pState;
  switch(uMsg)
  {
  case WM_CREATE:
    {
      int sox_result;

      CREATESTRUCT *pCreate = (CREATESTRUCT*)(lParam);
      pState = (StateInfo*)(pCreate->lpCreateParams);
      SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pState);
      sox_result = sox_init();
      if (sox_result != SOX_SUCCESS)
      {
        MessageBox(hwnd, 
                   L"An error occurred while initializing the sound system.",
                   L"ERROR",
                   MB_OK
        );
      }
      return sox_result;
    }
  case WM_COMMAND:
    switch(LOWORD(wParam))
    {
      case IDM_FILE_OPEN:
        {
          HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_ALL, &IID_IFileDialog, (void**)&pFileOpenDialog);
          if (SUCCEEDED(hr))
          {
            DWORD dwOptions;
            pFileOpenDialog->lpVtbl->GetOptions(pFileOpenDialog, &dwOptions);
            pFileOpenDialog->lpVtbl->SetOptions(pFileOpenDialog, dwOptions | FOS_PICKFOLDERS | FOS_FILEMUSTEXIST);

            hr = pFileOpenDialog->lpVtbl->Show(pFileOpenDialog, hwnd);
            if (SUCCEEDED(hr))
            {
              IShellItem* pSelectedItem = NULL;
              hr = pFileOpenDialog->lpVtbl->GetResult(pFileOpenDialog, &pSelectedItem);
              if (SUCCEEDED(hr))
              {
                PWSTR pszFolderPath = NULL;
                hr = pSelectedItem->lpVtbl->GetDisplayName(pSelectedItem, SIGDN_FILESYSPATH, &pszFolderPath);
                size_t filePathLength = wcslen(pszFolderPath);
                PWSTR filePathCopyManual = (PWSTR)CoTaskMemAlloc((filePathLength + 1) * sizeof(WCHAR));
                if (filePathCopyManual != NULL)
                {
                  wcscpy_s(filePathCopyManual, filePathLength + 1, pszFolderPath);
                  MessageBox(NULL, filePathCopyManual, L"Selected Folder", MB_OK);
                }
                CoTaskMemFree(pszFolderPath);
                CoTaskMemFree(filePathCopyManual);
                pSelectedItem->lpVtbl->Release(pSelectedItem);
              }
            }
            pFileOpenDialog->lpVtbl->Release(pFileOpenDialog);
            pFileOpenDialog = NULL;
          }
        }
        break;
      case IDM_FILE_EXIT:
        DestroyWindow(hwnd);
        break;
    }
    break;
  case WM_DESTROY:
    {
      int result;

      result = cleanup();
      if (result != 0)
      {
        MessageBox(hwnd, L"An error occurred while shutting down", L"ERROR", MB_OK);
      } 
      PostQuitMessage(0);
      return 0;
    }
  case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd, &ps);
      pState = GetAppState(hwnd);
      RECT rect;

      // All painting occurs here, between BeginPaint and EndPaint.
      FillRect(hdc, &ps.rcPaint, (HBRUSH) (COLOR_WINDOW+1));
      GetClientRect(hwnd, &rect);
      DrawTextEx(hdc,
        L"FILE SPLICER\n\nThis application splices all the .wav audio files in a directory. "\
          "The ordering of the files' contents in the output is determined by "\
          "the names of the files, so please make sure each filename starts with the correct track number. "\
          "Leading zeroes will be ignored. You can splice up to fifty files in a single directory.\n\n"\
          "The output file (spliced-audio.wav) will be placed in the same folder as the input files.\n\n"\
          "To get started, click 'Folder | Select' on the menu above.",
        -1, &rect,
        DT_CENTER | DT_EDITCONTROL | DT_WORDBREAK,
        NULL
      );
      EndPaint(hwnd, &ps);
    }
    return 0;
  }
  return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

