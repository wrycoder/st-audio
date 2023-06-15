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
#include <shlobj.h>
#include <objbase.h>
#include <string.h>
#include <strsafe.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <assert.h>


static sox_format_t * in, * out;
static int cleanup();

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

int compare_filenames(const void* a, const void* b)
{
  return strcmp(*(const char**)a, *(const char**)b);
}

const char* ConvertPWSTRToConstChar(PWSTR wideString)
{
  int length = WideCharToMultiByte(CP_UTF8, 0, wideString, -1, NULL, 0, NULL, NULL);
  char* buffer = (char*)malloc(length * sizeof(char));
  if (buffer == NULL)
  {
    MessageBox(NULL, L"Filed to allocate memory for converted string", L"ERROR", MB_OK);
    cleanup();
    exit(1);
  }
  if (WideCharToMultiByte(CP_UTF8, 0, wideString, -1, buffer, length, NULL, NULL) == 0)
  {
    MessageBox(NULL, L"Filed to convert wide string to UTF-8", L"ERROR", MB_OK);
    free(buffer);
    cleanup();
    exit(1);
  }
  return buffer;
}

void list_files_sorted(PWSTR directory_path)
{
  DIR* directory;
  struct dirent* entry;
  char** filenames;
  int file_count = 0;

  directory = opendir(ConvertPWSTRToConstChar(directory_path));
  if (directory == NULL)
  {
    size_t dirPathLength = wcslen(directory_path);
    PWSTR buffer = (PWSTR)CoTaskMemAlloc((dirPathLength + 1) * sizeof(WCHAR));
    wcscpy_s(buffer, sizeof(buffer), directory_path);
    MessageBox(NULL, buffer, L"ERROR", MB_OK);
    return;
  }

  while ((entry = readdir(directory)) != NULL)
  {
    const char* file_name = entry->d_name;
    char file_path[MAX_PATH];
    StringCbPrintf(file_path, sizeof(file_path), "%s\\%s", directory_path, file_name);

    DWORD file_attributes = GetFileAttributes(file_path);
    if (file_attributes != INVALID_FILE_ATTRIBUTES &&
        !(file_attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
      file_count++;
    }
  }

  TCHAR msgbuf[75];
  StringCbPrintf(msgbuf, sizeof(msgbuf), "File Count: %d\n", file_count);
  MessageBox(NULL, msgbuf, L"DUDE", MB_OK);
  filenames = (char**)malloc(file_count * sizeof(char*));

  rewinddir(directory);

  int i = 0;
  while((entry = readdir(directory)) != NULL)
  {
    const char* file_name = entry->d_name;
    char file_path[MAX_PATH];
    StringCbPrintf(file_path, sizeof(file_path), "%s\\%s", directory_path, file_name);

    DWORD file_attributes = GetFileAttributes(file_path);
    if (file_attributes != INVALID_FILE_ATTRIBUTES &&
        !(file_attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
      filenames[i] = strdup(entry->d_name);
      i++;
    }
  }

  qsort(filenames, file_count, sizeof(char *), compare_filenames);

  PWSTR buffer = (PWSTR)malloc((MAX_PATH + 1) * sizeof(wchar_t));
  TCHAR result[MAX_PATH * 10] = L""; // Assuming an average filename length

  for (int j = 0; j < file_count; j++)
  {
    StringCbPrintf(buffer, sizeof(buffer), "%s\n", filenames[j]);
    MessageBox(NULL, buffer, L"Sorted Filenames", MB_OK);
    StringCbCat(result, sizeof(result), buffer);
    free(filenames[j]);
  }

  free(filenames);

  MessageBox(NULL, result, L"Sorted Filenames", MB_OK);

  free(buffer);
  closedir(directory);
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

 /* Global variables */
HMENU hMenu;
IFileDialog* pFileOpenDialog;
#define APP_WINDOW_HEIGHT   350
#define APP_WINDOW_WIDTH    450
#define IDM_FILE_OPEN       1
#define IDM_FILE_EXIT       3

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

PWSTR              pszWorkingDirectory;

/* Splice the audio files using SoX */
DWORD WINAPI SpliceThreadProc() {
  list_files_sorted(pszWorkingDirectory);
  return 0;
}

#define NOMINMAX // from example on stackoverflow.com

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
  const wchar_t CLASS_NAME[] = L"Splicing Audio Files";

  WNDCLASS wc = { };

  wc.lpfnWndProc    = WindowProc;
  wc.hInstance      = hInstance;
  wc.lpszClassName  = CLASS_NAME;

  RegisterClass(&wc);

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
    CW_USEDEFAULT, CW_USEDEFAULT, APP_WINDOW_WIDTH, APP_WINDOW_HEIGHT,

    NULL,                   // Parent window
    hMenu,                  // Menu
    hInstance,              // Instance handle
    NULL                    // Additional application data
  );

  if (hwnd == NULL)
  {
    return 0;
  }

  hMenu = CreateMenu();
  HMENU hFileMenu = CreateMenu();

  AppendMenu(hMenu, MF_POPUP, (UINT_PTR)hFileMenu, L"Folder");
  AppendMenu(hFileMenu, MF_STRING, IDM_FILE_OPEN, L"Select");
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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch(uMsg)
  {
  case WM_CREATE:
    {
      int sox_result;
      DWORD dwRet;

      CREATESTRUCT *pCreate = (CREATESTRUCT*)(lParam);
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
            pFileOpenDialog->lpVtbl->SetOkButtonLabel(pFileOpenDialog, L"Select Folder");

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
                pszWorkingDirectory = (PWSTR)CoTaskMemAlloc((filePathLength + 1) * sizeof(WCHAR));
                if (pszWorkingDirectory != NULL)
                {
                  wcscpy_s(pszWorkingDirectory, filePathLength + 1, pszFolderPath);
                  MessageBox(NULL, pszWorkingDirectory, L"Selected Folder", MB_OK);
                  if (filePathLength < MAX_PATH)
                  {
                    SetCurrentDirectory(pszWorkingDirectory);
                    DWORD dwThreadId;
                    HANDLE hThread = CreateThread(NULL, 0, SpliceThreadProc, NULL, 0, &dwThreadId);
                    if (hThread != NULL)
                    {
                      CloseHandle(hThread);
                    } else {
                      MessageBox(NULL, L"Unable to splice audio files.", L"ERROR", MB_OK);
                    }
                  } else {
                    MessageBox(NULL, L"Selected folder is invalid", L"ERROR", MB_OK);
                  }
                }
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

