/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

#include "utils/BaseUtil.h"
#include "utils/Dpi.h"
#include "utils/Timer.h"
#include "utils/WinUtil.h"

#include "wingui/UIModels.h"

#include "wingui/Layout.h"
#include "wingui/FrameRateWnd.h"

#include "AppColors.h"
#include "Settings.h"
#include "Controller.h"
#include "GlobalPrefs.h"
#include "SumatraConfig.h"
#include "SumatraPDF.h"
#include "WindowInfo.h"
#include "resource.h"
#include "Commands.h"
#include "Canvas.h"
#include "Menu.h"
#include "SumatraAbout.h"
#include "Translations.h"

static void OnPaintAbout(WindowInfo* win) {
    auto t = TimeGet();
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(win->hwndCanvas, &ps);

    auto txtCol = GetAppColor(AppColor::MainWindowText);
    auto bgCol = GetAppColor(AppColor::MainWindowBg);
    if (HasPermission(Perm::SavePreferences | Perm::DiskAccess) && gGlobalPrefs->rememberOpenedFiles &&
        gGlobalPrefs->showStartPage) {
        DrawStartPage(win, win->buffer->GetDC(), gFileHistory, txtCol, bgCol);
    } else {
        DrawAboutPage(win, win->buffer->GetDC());
    }
    win->buffer->Flush(hdc);

    EndPaint(win->hwndCanvas, &ps);
    if (gShowFrameRate) {
        win->frameRateWnd->ShowFrameRateDur(TimeSinceInMs(t));
    }
}

static void OnMouseLeftButtonDownAbout(WindowInfo* win, int x, int y, WPARAM) {
    // lf("Left button clicked on %d %d", x, y);

    // remember a link under so that on mouse up we only activate
    // link if mouse up is on the same link as mouse down
    win->urlOnLastButtonDown = GetStaticLink(win->staticLinks, x, y, nullptr);
}

static bool IsLink(const WCHAR* url) {
    if (str::StartsWithI(url, L"http:")) {
        return true;
    }
    if (str::StartsWithI(url, L"https:")) {
        return true;
    }
    if (str::StartsWithI(url, L"mailto:")) {
        return true;
    }
    return false;
}

static void OnMouseLeftButtonUpAbout(WindowInfo* win, int x, int y, WPARAM) {
    SetFocus(win->hwndFrame);

    const WCHAR* url = GetStaticLink(win->staticLinks, x, y, nullptr);
    const WCHAR* prevUrl = win->urlOnLastButtonDown;
    win->urlOnLastButtonDown = nullptr;
    if (!url || url != prevUrl) {
        return;
    }
    if (str::Eq(url, kLinkOpenFile)) {
        HwndSendCommand(win->hwndFrame, CmdOpenFile);
    } else if (str::Eq(url, kLinkHideList)) {
        gGlobalPrefs->showStartPage = false;
        win->RedrawAll(true);
    } else if (str::Eq(url, kLinkShowList)) {
        gGlobalPrefs->showStartPage = true;
        win->RedrawAll(true);
    } else if (IsLink(url)) {
        SumatraLaunchBrowser(url);
    } else {
        // assume it's a document
        LoadArgs args(url, win);
        LoadDocument(args);
    }
}

static void OnMouseRightButtonDownAbout(WindowInfo* win, int x, int y, WPARAM) {
    // lf("Right button clicked on %d %d", x, y);
    SetFocus(win->hwndFrame);
    win->dragStart = Point(x, y);
}

static void OnMouseRightButtonUpAbout(WindowInfo* win, int x, int y, WPARAM) {
    int isDrag = IsDrag(x, win->dragStart.x, y, win->dragStart.y);
    if (isDrag) {
        return;
    }
    OnAboutContextMenu(win, x, y);
}

static LRESULT OnSetCursorAbout(WindowInfo* win, HWND hwnd) {
    Point pt;
    if (GetCursorPosInHwnd(hwnd, pt)) {
        StaticLinkInfo* linkInfo;
        if (GetStaticLink(win->staticLinks, pt.x, pt.y, &linkInfo)) {
            win->ShowToolTip(linkInfo->infotip, linkInfo->rect);
            SetCursorCached(IDC_HAND);
        } else {
            win->HideToolTip();
            SetCursorCached(IDC_ARROW);
        }
        return TRUE;
    }

    win->HideToolTip();
    return FALSE;
}

LRESULT WndProcCanvasAbout(WindowInfo* win, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    int x = GET_X_LPARAM(lp);
    int y = GET_Y_LPARAM(lp);
    switch (msg) {
        case WM_LBUTTONDOWN:
            OnMouseLeftButtonDownAbout(win, x, y, wp);
            return 0;

        case WM_LBUTTONUP:
            OnMouseLeftButtonUpAbout(win, x, y, wp);
            return 0;

        case WM_LBUTTONDBLCLK:
            OnMouseLeftButtonDownAbout(win, x, y, wp);
            return 0;

        case WM_RBUTTONDOWN:
            OnMouseRightButtonDownAbout(win, x, y, wp);
            return 0;

        case WM_RBUTTONUP:
            OnMouseRightButtonUpAbout(win, x, y, wp);
            return 0;

        case WM_SETCURSOR:
            if (OnSetCursorAbout(win, hwnd)) {
                return TRUE;
            }
            return DefWindowProc(hwnd, msg, wp, lp);

        case WM_CONTEXTMENU:
            OnAboutContextMenu(win, 0, 0);
            return 0;

        case WM_PAINT:
            OnPaintAbout(win);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wp, lp);
    }
}
