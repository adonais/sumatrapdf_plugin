/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: Simplified BSD (see COPYING.BSD) */

class FrameSite;
class HtmlMoniker;

bool IsBlankUrl(const WCHAR*);

// HtmlWindowCallback allows HtmlWindow to notify other code about notable
// events or delegate some of the functionality.
class HtmlWindowCallback {
  public:
    // called when we're about to show a given url. Returning false will
    // stop loading this url
    virtual bool OnBeforeNavigate(const WCHAR* url, bool newWindow) = 0;

    // called after html document has been completely loaded
    virtual void OnDocumentComplete(const WCHAR* url) = 0;

    // allows for providing data for a given url.
    // returning nullptr means data wasn't provided.
    virtual ByteSlice GetDataForUrl(const WCHAR* url) = 0;

    // called when left mouse button is clicked in the web control window.
    // we use it to maintain proper focus (since it's stolen by left click)
    virtual void OnLButtonDown() = 0;

    // called when a file can't be displayed and has to be downloaded instead
    virtual void DownloadData(const WCHAR* url, ByteSlice data) = 0;

    virtual ~HtmlWindowCallback() = default;
};

// HtmlWindow embeds a web browser (Internet Explorer) control
// inside provided HWND so that an app can display html content.
class HtmlWindow {
  protected:
    friend class FrameSite;

    int windowId = 0;
    HWND hwndParent = nullptr;
    IWebBrowser2* webBrowser = nullptr;
    IOleObject* oleObject = nullptr;
    IOleInPlaceObject* oleInPlaceObject = nullptr;
    IViewObject* viewObject = nullptr;
    IConnectionPoint* connectionPoint = nullptr;
    HtmlMoniker* htmlContent = nullptr;
    HWND oleObjectHwnd = nullptr;
    int zoomDPI = 96;

    const char* htmlSetInProgress = nullptr;
    const WCHAR* htmlSetInProgressUrl = nullptr;

    DWORD adviseCookie = 0;
    bool blankWasShown = false;

    AutoFreeWstr currentURL;

    HtmlWindow(HWND hwndParent, HtmlWindowCallback* cb);

    void NavigateToAboutBlank();
    bool CreateBrowser();

    void SubclassHwnd();
    void UnsubclassHwnd();
    void SetScrollbarToAuto();
    void SetHtmlReal(ByteSlice);
    void FreeHtmlSetInProgressData();

  public:
    ~HtmlWindow();

    void OnSize(Size size);
    void SetVisible(bool visible);
    void NavigateToUrl(const WCHAR* url);
    void NavigateToDataUrl(const WCHAR* url);
    void SetHtml(ByteSlice, const WCHAR* url = nullptr);
    void GoBack();
    void GoForward();
    void PrintCurrentPage(bool showUI);
    void SetZoomPercent(int zoom);
    int GetZoomPercent();
    void FindInCurrentPage();
    void SelectAll();
    void CopySelection();
    LRESULT SendMsg(uint msg, WPARAM wp, LPARAM lp);
    void OnLButtonDown() const;
    HBITMAP TakeScreenshot(Rect area, Size finalSize);

    bool canGoBack = false;
    bool canGoForward = false;

    // TODO: not for public use
    WNDPROC wndProcBrowserPrev = nullptr;
    LONG_PTR userDataBrowserPrev = 0;
    HtmlWindowCallback* htmlWinCb = nullptr;

    bool OnBeforeNavigate(const WCHAR* url, bool newWindow);
    void OnDocumentComplete(const WCHAR* url);
    HRESULT OnDragEnter(IDataObject* dataObj);
    HRESULT OnDragDrop(IDataObject* dataObj);

    static HtmlWindow* Create(HWND hwndParent, HtmlWindowCallback* cb);
};
