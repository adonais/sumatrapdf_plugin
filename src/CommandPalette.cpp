/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

#include "utils/BaseUtil.h"
#include "utils/WinUtil.h"
#include "utils/UITask.h"
#include "utils/FileUtil.h"

#include "wingui/UIModels.h"
#include "wingui/Layout.h"
#include "wingui/wingui2.h"

#include "Settings.h"
#include "Controller.h"
#include "EngineBase.h"
#include "EngineAll.h"
#include "GlobalPrefs.h"
#include "DisplayModel.h"
#include "WindowInfo.h"
#include "TabInfo.h"
#include "SumatraConfig.h"
#include "Commands.h"
#include "CommandPalette.h"
#include "SumatraPDF.h"
#include "Tabs.h"
#include "ExternalViewers.h"
#include "Annotation.h"

#include "utils/Log.h"

using namespace wg;

static HFONT gCommandPaletteFont = nullptr;

// clang-format off
static i32 gBlacklistCommandsFromPalette[] = {
    CmdNone,
    CmdOpenWithFirst,
    CmdOpenWithLast,
    CmdCommandPalette,

    // managing frequently list in home tab
    CmdOpenSelectedDocument,
    CmdPinSelectedDocument,
    CmdForgetSelectedDocument,

    CmdExpandAll,   // TODO: figure proper context for it
    CmdCollapseAll, // TODO: figure proper context for it
    CmdMoveFrameFocus,

    CmdFavoriteAdd,
    CmdFavoriteDel,

    CmdPresentationWhiteBackground,
    CmdPresentationBlackBackground,

    CmdSaveEmbeddedFile, // TODO: figure proper context for it
    CmdCreateShortcutToFile, // not sure I want this at all
};

// most commands are not valid when document is not opened
// it's shorter to list the remaining commands
static i32 gDocumentNotOpenWhitelist[] = {
    CmdOpenFile,
    CmdOpenFolder,
    CmdExit,
    CmdNewWindow,
    CmdContributeTranslation,
    CmdOptions,
    CmdAdvancedOptions,
    CmdChangeLanguage,
    CmdCheckUpdate,
    CmdHelpOpenManualInBrowser,
    CmdHelpVisitWebsite,
    CmdHelpAbout,
    CmdDebugDownloadSymbols,
    CmdFavoriteToggle,
    CmdToggleFullscreen,
    CmdToggleMenuBar,
    CmdShowLog,
};

// for those commands do not activate main window
// for example those that show dialogs (because the main window takes
// focus away from them)
static i32 gCommandsNoActivate[] = {
    CmdOptions,
    CmdChangeLanguage,
    CmdHelpAbout,
    CmdHelpOpenManualInBrowser,
    CmdHelpVisitWebsite,
    CmdOpenFile,
    CmdOpenFolder,
    // TOOD: probably more
};
// clang-format on

// those are shared with Menu.cpp
extern UINT_PTR removeIfAnnotsNotSupported[];
extern UINT_PTR disableIfNoSelection[];

extern UINT_PTR removeIfNoInternetPerms[];
extern UINT_PTR removeIfNoFullscreenPerms[];
extern UINT_PTR removeIfNoPrefsPerms[];
extern UINT_PTR removeIfNoDiskAccessPerm[];
extern UINT_PTR removeIfNoCopyPerms[];
extern UINT_PTR removeIfChm[];

static bool __cmdInList(i32 cmdId, i32* ids, int nIds) {
    for (int i = 0; i < nIds; i++) {
        if (ids[i] == cmdId) {
            return true;
        }
    }
    return false;
}

// a must end with sentinel value of 0
static bool IsCmdInMenuList(i32 cmdId, UINT_PTR* a) {
    UINT_PTR id = (UINT_PTR)cmdId;
    for (int i = 0; a[i]; i++) {
        if (a[i] == id) {
            return true;
        }
    }
    return false;
}

#define IsCmdInList(name) __cmdInList(cmdId, name, dimof(name))

struct CommandPaletteWnd : Wnd {
    ~CommandPaletteWnd() override = default;
    WindowInfo* win = nullptr;

    Edit* editQuery = nullptr;
    StrVec allStrings;
    // maps original file path to converted file path
    StrVec convertedFilePaths;
    ListBox* listBox = nullptr;
    Static* staticHelp = nullptr;

    void OnDestroy() override;
    bool PreTranslateMessage(MSG& msg) override;
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) override;

    void ScheduleDelete();

    bool Create(WindowInfo* win);
    void QueryChanged();
    void ListDoubleClick();

    void ExecuteCurrentSelection();
};

struct CommandPaletteBuildCtx {
    bool isDocLoaded = false;
    bool supportsAnnots = false;
    bool hasSelection = false;
    bool isChm = false;
    bool canSendEmail = false;
    Annotation* annotationUnderCursor = nullptr;
    bool hasUnsavedAnnotations = false;
    bool isCursorOnPage = false;
    bool cursorOnLinkTarget = false;
    bool cursorOnComment = false;
    bool cursorOnImage = false;
    bool hasToc = false;
    bool allowToggleMenuBar = false;

    ~CommandPaletteBuildCtx();
};
CommandPaletteBuildCtx::~CommandPaletteBuildCtx() {
    delete annotationUnderCursor;
}

static bool AllowCommand(const CommandPaletteBuildCtx& ctx, i32 cmdId) {
    if (IsCmdInList(gBlacklistCommandsFromPalette)) {
        return false;
    }

    if (!ctx.isDocLoaded) {
        if (!IsCmdInList(gDocumentNotOpenWhitelist)) {
            return false;
        }
    }

    if (cmdId == CmdToggleMenuBar) {
        return ctx.allowToggleMenuBar;
    }

    if (!ctx.supportsAnnots) {
        if ((cmdId >= (i32)CmdCreateAnnotFirst) && (cmdId <= (i32)CmdCreateAnnotLast)) {
            return false;
        }
        if (IsCmdInMenuList(cmdId, removeIfAnnotsNotSupported)) {
            return false;
        }
    }

    if (!ctx.hasSelection && IsCmdInMenuList(cmdId, disableIfNoSelection)) {
        return false;
    }

    if (ctx.isChm && IsCmdInMenuList(cmdId, removeIfChm)) {
        return false;
    }

    if (!ctx.canSendEmail && (cmdId == CmdSendByEmail)) {
        return false;
    }

    if (!ctx.annotationUnderCursor) {
        if ((cmdId == CmdSelectAnnotation) || (cmdId == CmdDeleteAnnotation)) {
            return false;
        }
    }

    if ((cmdId == CmdSaveAnnotations) && !ctx.hasUnsavedAnnotations) {
        return false;
    }

    if ((cmdId == CmdCheckUpdate) && gIsPluginBuild) {
        return false;
    }

    bool remove = false;
    if (!HasPermission(Perm::InternetAccess)) {
        remove |= IsCmdInMenuList(cmdId, removeIfNoInternetPerms);
    }
    if (!HasPermission(Perm::FullscreenAccess)) {
        remove |= IsCmdInMenuList(cmdId, removeIfNoFullscreenPerms);
    }
    if (!HasPermission(Perm::SavePreferences)) {
        remove |= IsCmdInMenuList(cmdId, removeIfNoPrefsPerms);
    }
    if (!HasPermission(Perm::PrinterAccess)) {
        remove |= (cmdId == CmdPrint);
    }
    if (!HasPermission(Perm::DiskAccess)) {
        remove |= IsCmdInMenuList(cmdId, removeIfNoDiskAccessPerm);
    }
    if (!HasPermission(Perm::CopySelection)) {
        remove |= IsCmdInMenuList(cmdId, removeIfNoCopyPerms);
    }
    if (remove) {
        return false;
    }

    if (!ctx.cursorOnLinkTarget && (cmdId == CmdCopyLinkTarget)) {
        return false;
    }
    if (!ctx.cursorOnComment && (cmdId == CmdCopyComment)) {
        return false;
    }
    if (!ctx.cursorOnImage && (cmdId == CmdCopyImage)) {
        return false;
    }
    if (!ctx.hasToc && (cmdId == CmdToggleBookmarks)) {
        return false;
    }
    if ((cmdId == CmdToggleScrollbars) && !gGlobalPrefs->fixedPageUI.hideScrollbars) {
        return false;
    }

    switch (cmdId) {
        case CmdDebugShowLinks:
        // case CmdDebugAnnotations:
        // case CmdDebugDownloadSymbols:
        case CmdDebugTestApp:
        case CmdDebugShowNotif:
        case CmdDebugCrashMe: {
            return gIsDebugBuild || gIsPreReleaseBuild;
        }
    }
    return true;
}

static char* ConvertPathForDisplayTemp(const char* s) {
    const char* name = path::GetBaseNameTemp(s);
    char* dir = path::GetDirTemp(s);
    char* res = str::JoinTemp(name, "  (", dir);
    res = str::JoinTemp(res, ")");
    return res;
}

static void AddOpenedFiles(StrVec& strings, StrVec& filePaths, WindowInfo* win) {
    for (TabInfo* tab : win->tabs) {
        if (!tab->IsDocLoaded()) {
            continue;
        }
        auto path = tab->filePath.Get();
        char* s = ToUtf8Temp(path);
        filePaths.AppendIfNotExists(s);
        s = (char*)path::GetBaseNameTemp(s);
        // s = ConvertPathForDisplayTemp(s);
        filePaths.AppendIfNotExists(s);
        // avoid adding the same file opened in multiple window
        strings.AppendIfNotExists(s);
    }
}

static TabInfo* FindOpenedFile(std::string_view sv) {
    for (WindowInfo* win : gWindows) {
        for (TabInfo* tab : win->tabs) {
            if (!tab->IsDocLoaded()) {
                continue;
            }
            auto path = tab->filePath.Get();
            char* s = ToUtf8Temp(path);
            if (str::Eq(s, sv.data())) {
                return tab;
            }
        }
    }
    return nullptr;
}

static void CollectPaletteStrings(StrVec& strings, StrVec& filePaths, WindowInfo* win) {
    CommandPaletteBuildCtx ctx;
    ctx.isDocLoaded = win->IsDocLoaded();
    TabInfo* tab = win->currentTab;
    ctx.hasSelection = ctx.isDocLoaded && tab && win->showSelection && tab->selectionOnPage;
    ctx.canSendEmail = CanSendAsEmailAttachment(tab);
    ctx.allowToggleMenuBar = !win->tabsInTitlebar;

    Point cursorPos;
    GetCursorPosInHwnd(win->hwndCanvas, cursorPos);

    DisplayModel* dm = win->AsFixed();
    if (dm) {
        auto engine = dm->GetEngine();
        ctx.supportsAnnots = EngineSupportsAnnotations(engine);
        ctx.hasUnsavedAnnotations = EngineHasUnsavedAnnotations(engine);
        int pageNoUnderCursor = dm->GetPageNoByPoint(cursorPos);
        if (pageNoUnderCursor > 0) {
            ctx.isCursorOnPage = true;
        }
        ctx.annotationUnderCursor = dm->GetAnnotationAtPos(cursorPos, nullptr);

        PointF ptOnPage = dm->CvtFromScreen(cursorPos, pageNoUnderCursor);
        IPageElement* pageEl = dm->GetElementAtPos(cursorPos, nullptr);
        if (pageEl) {
            WCHAR* value = pageEl->GetValue();
            ctx.cursorOnLinkTarget = value && pageEl->Is(kindPageElementDest);
            ctx.cursorOnComment = value && pageEl->Is(kindPageElementComment);
            ctx.cursorOnImage = pageEl->Is(kindPageElementImage);
        }
    }

    ctx.hasToc = win->ctrl && win->ctrl->HacToc();

    // append paths of opened files
    for (WindowInfo* w : gWindows) {
        AddOpenedFiles(strings, filePaths, w);
    }
    // append paths of files from history, excluding
    // already appended (from opened files)
    for (FileState* fs : *gGlobalPrefs->fileStates) {
        char* s = fs->filePath;
        filePaths.Append(s);
        s = ConvertPathForDisplayTemp(s);
        filePaths.Append(s);
        strings.AppendIfNotExists(s);
    }

    // we want the commands sorted
    StrVec tempStrings;
    int cmdId = (int)CmdFirst + 1;
    for (SeqStrings strs = gCommandDescriptions; strs; seqstrings::Next(strs, cmdId)) {
        if (AllowCommand(ctx, (i32)cmdId)) {
            CrashIf(str::Len(strs) == 0);
            tempStrings.Append(strs);
        }
    }
    StrVecSortedView sortedView;
    tempStrings.GetSortedViewNoCase(sortedView);
    int n = sortedView.Size();
    for (int i = 0; i < n; i++) {
        auto sv = sortedView.at(i);
        strings.Append(sv.data());
    }
}

// filter is one or more words separated by whitespace
// filter matches if all words match, ignoring the case
static bool FilterMatches(const char* str, const char* filter) {
    // empty filter matches all
    if (!filter || str::EmptyOrWhiteSpaceOnly(filter)) {
        return true;
    }
    StrVec words;
    char* s = str::DupTemp(filter);
    char* wordStart = s;
    bool wasWs = false;
    while (*s) {
        if (str::IsWs(*s)) {
            *s = 0;
            if (!wasWs) {
                words.AppendIfNotExists(wordStart);
                wasWs = true;
            }
            wordStart = s + 1;
        }
        s++;
    }
    if (str::Len(wordStart) > 0) {
        words.AppendIfNotExists(wordStart);
    }
    // all words must be present
    int nWords = words.Size();
    for (int i = 0; i < nWords; i++) {
        auto word = words.at(i);
        if (!str::ContainsI(str, word.data())) {
            return false;
        }
    }
    return true;
}

static void FilterStrings(const StrVec& strs, const char* filter, StrVec& matchedOut) {
    matchedOut.Reset();
    int n = strs.Size();
    for (int i = 0; i < n; i++) {
        auto s = strs.at(i);
        if (!FilterMatches(s.data(), filter)) {
            continue;
        }
        matchedOut.Append(s.data());
    }
}

LRESULT CommandPaletteWnd::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_ACTIVATE:
            if (wparam == WA_INACTIVE) {
                ScheduleDelete();
                return 0;
            }
            break;
    }

    return WndProcDefault(hwnd, msg, wparam, lparam);
}

bool CommandPaletteWnd::PreTranslateMessage(MSG& msg) {
    if (msg.message == WM_KEYDOWN) {
        int dir = 0;
        if (msg.wParam == VK_ESCAPE) {
            ScheduleDelete();
            return true;
        }

        if (msg.wParam == VK_RETURN) {
            ExecuteCurrentSelection();
            return true;
        }

        if (msg.wParam == VK_UP) {
            dir = -1;
        } else if (msg.wParam == VK_DOWN) {
            dir = 1;
        }
        if (!dir) {
            return false;
        }
        int n = listBox->GetCount();
        if (n == 0) {
            return false;
        }
        int currSel = listBox->GetCurrentSelection();
        int sel = currSel + dir;
        if (sel < 0) {
            sel = n - 1;
        }
        if (sel >= n) {
            sel = 0;
        }
        listBox->SetCurrentSelection(sel);
    }
    return false;
}

void CommandPaletteWnd::QueryChanged() {
    auto filter = editQuery->GetText();
    // for efficiency, reusing existing model
    auto m = (ListBoxModelStrings*)listBox->model;
    FilterStrings(allStrings, filter.Get(), m->strings);
    listBox->SetModel(m);
    if (m->ItemsCount() > 0) {
        listBox->SetCurrentSelection(0);
    }
}

static CommandPaletteWnd* gCommandPaletteWnd = nullptr;
static HWND gHwndToActivateOnClose = nullptr;

void SafeDeleteCommandPaletteWnd() {
    if (!gCommandPaletteWnd) {
        return;
    }

    auto tmp = gCommandPaletteWnd;
    gCommandPaletteWnd = nullptr;
    delete tmp;
    if (gHwndToActivateOnClose) {
        SetActiveWindow(gHwndToActivateOnClose);
        gHwndToActivateOnClose = nullptr;
    }
}

void CommandPaletteWnd::ScheduleDelete() {
    uitask::Post(&SafeDeleteCommandPaletteWnd);
}

void CommandPaletteWnd::ExecuteCurrentSelection() {
    int sel = listBox->GetCurrentSelection();
    if (sel < 0) {
        return;
    }
    auto m = (ListBoxModelStrings*)listBox->model;
    const char* s = m->Item(sel).data();
    int cmdId = GetCommandIdByDesc(s);
    if (cmdId >= 0) {
        bool noActivate = IsCmdInList(gCommandsNoActivate);
        if (noActivate) {
            gHwndToActivateOnClose = nullptr;
        }
        HwndSendCommand(win->hwndFrame, cmdId);
        ScheduleDelete();
        return;
    }

    int n = convertedFilePaths.Size() / 2;
    bool isFromTab = false;
    for (int i = 0; i < n; i++) {
        const char* converted = convertedFilePaths.at(i * 2 + 1).data();
        if (!str::Eq(converted, s)) {
            continue;
        }
        s = convertedFilePaths.at(i * 2).data();
        // a hack-ish detection of filename from tab
        // vs. from history. Name from tab are only file names
        // and therefore much shorter than full path (converted)
        if (str::Len(s) > str::Len(converted) + 3) {
            isFromTab = true;
        }
        break;
    }

    TabInfo* tab = FindOpenedFile(s);
    if (isFromTab && (tab != nullptr)) {
        if (tab->win->currentTab != tab) {
            SelectTabInWindow(tab);
        }
        gHwndToActivateOnClose = tab->win->hwndFrame;
        ScheduleDelete();
        return;
    }

    LoadArgs args(s, win);
    args.forceReuse = false; // open in a new tab
    LoadDocument(args);
    ScheduleDelete();
}

void CommandPaletteWnd::ListDoubleClick() {
    ExecuteCurrentSelection();
}

void CommandPaletteWnd::OnDestroy() {
    ScheduleDelete();
}

// almost like HwndPositionInCenterOf but y is near top of hwndRelative
static void PositionCommandPalette(HWND hwnd, HWND hwndRelative) {
    Rect rRelative = WindowRect(hwndRelative);
    Rect r = WindowRect(hwnd);
    int x = rRelative.x + (rRelative.dx / 2) - (r.dx / 2);
    int y = rRelative.y + (rRelative.dy / 2) - (r.dy / 2);

    Rect rc = ShiftRectToWorkArea(Rect{x, y, r.dx, r.dy}, hwnd, true);
    rc.y = rRelative.y + 32;
    SetWindowPos(hwnd, nullptr, rc.x, rc.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

bool CommandPaletteWnd::Create(WindowInfo* win) {
    CollectPaletteStrings(allStrings, convertedFilePaths, win);
    {
        CreateCustomArgs args;
        // args.title = L"Command Palette";
        args.visible = false;
        args.style = WS_POPUPWINDOW;
        args.font = gCommandPaletteFont;
        CreateCustom(args);
    }
    if (!hwnd) {
        return false;
    }

    auto vbox = new VBox();
    vbox->alignMain = MainAxisAlign::MainStart;
    vbox->alignCross = CrossAxisAlign::Stretch;

    {
        EditCreateArgs args;
        args.parent = hwnd;
        args.isMultiLine = false;
        args.withBorder = true;
        args.cueText = "a cue text";
        args.font = gCommandPaletteFont;
        auto c = new Edit();
        c->maxDx = 150;
        c->onTextChanged = std::bind(&CommandPaletteWnd::QueryChanged, this);
        HWND ok = c->Create(args);
        CrashIf(!ok);
        editQuery = c;
        vbox->AddChild(c);
    }

    {
        ListBoxCreateArgs args;
        args.parent = hwnd;
        args.font = gCommandPaletteFont;
        auto c = new ListBox();
        c->onDoubleClick = std::bind(&CommandPaletteWnd::ListDoubleClick, this);
        c->idealSizeLines = 32;
        c->SetInsetsPt(4, 0);
        auto wnd = c->Create(args);
        CrashIf(!wnd);

        auto m = new ListBoxModelStrings();
        FilterStrings(allStrings, nullptr, m->strings);
        c->SetModel(m);
        listBox = c;
        vbox->AddChild(c, 1);
    }

    {
        StaticCreateArgs args;
        args.parent = hwnd;
        args.font = gCommandPaletteFont;
        args.text = "↑ ↓ to navigate      Enter to select     Esc to close";

        auto c = new Static();
        auto wnd = c->Create(args);
        CrashIf(!wnd);
        staticHelp = c;
        vbox->AddChild(c);
    }

    auto padding = new Padding(vbox, DpiScaledInsets(hwnd, 4, 8));
    layout = padding;

    auto rc = ClientRect(win->hwndFrame);
    int dy = rc.dy - 72;
    if (dy < 480) {
        dy = 480;
    }
    int dx = rc.dx - 256;
    if (dx < 640) {
        dx = 640;
    }
    LayoutAndSizeToContent(layout, dx, dy, hwnd);
    PositionCommandPalette(hwnd, win->hwndFrame);

    SetIsVisible(true);
    ::SetFocus(editQuery->hwnd);
    return true;
}

void RunCommandPallette(WindowInfo* win) {
    CrashIf(gCommandPaletteWnd);
    // make min font size 16 (I get 12)
    int fontSize = GetSizeOfDefaultGuiFont();
    // make font 1.4x bigger than system font
    fontSize = (fontSize * 14) / 10;
    if (fontSize < 16) {
        fontSize = 16;
    }
    gCommandPaletteFont = GetDefaultGuiFontOfSize(fontSize);
    // TODO: leaking font

    auto wnd = new CommandPaletteWnd();
    wnd->win = win;
    bool ok = wnd->Create(win);
    CrashIf(!ok);
    gCommandPaletteWnd = wnd;
    gHwndToActivateOnClose = win->hwndFrame;
}
