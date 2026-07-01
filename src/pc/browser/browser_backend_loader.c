#if defined(_WIN32) && defined(ENABLE_BROWSER) && ENABLE_BROWSER

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "browser_backend.h"
#include "browser_backend_api.h"

#include "pc/debuglog.h"

#define BROWSER_BACKEND_DLL_NAME "browser_backend_cef.dll"
#define BROWSER_BACKEND_RUNTIME_DIR "cef_resources"

struct BrowserBackendBrowser {
    void *handle;
};

static HMODULE sBrowserBackendModule = NULL;
static const struct BrowserBackendApi *sBrowserBackendApi = NULL;
static bool sBrowserBackendInited = false;
static bool sBrowserBackendAvailable = false;

static void browser_backend_notify_load(s32 browserId, const char *url) {
    browser_manager_backend_notify_load(browserId, url);
}

static void browser_backend_notify_error(s32 browserId, const char *message, const char *details) {
    browser_manager_backend_notify_error(browserId, message, details);
}

static void browser_backend_notify_console(s32 browserId, const char *message, s32 level) {
    browser_manager_backend_notify_console(browserId, message, level);
}

static void browser_backend_notify_message(s32 browserId, const char *message) {
    browser_manager_backend_notify_message(browserId, message);
}

static void browser_backend_notify_function_call(s32 browserId, s32 bindingId, const char *argsPayload) {
    browser_manager_backend_notify_function_call(browserId, bindingId, argsPayload);
}

static void browser_backend_notify_paint(s32 browserId, s32 width, s32 height) {
    browser_manager_backend_notify_paint(browserId, width, height);
}

static void browser_backend_notify_js_result(s32 browserId, s32 requestId, s32 success, const char *result, const char *errorMessage) {
    browser_manager_backend_notify_js_result(browserId, requestId, success != 0, result, errorMessage);
}

static int32_t browser_backend_should_load_url(s32 browserId, const char *url) {
    return browser_manager_backend_should_load_url(browserId, url) ? 1 : 0;
}

static int32_t browser_backend_resolve_url_to_path(s32 browserId, const char *url, char *path, uint32_t pathCapacity) {
    return browser_manager_backend_resolve_url_to_path(browserId, url, path, (size_t) pathCapacity) ? 1 : 0;
}

static void browser_backend_fill_api_surface(struct BrowserBackendApiSurface *dst, const struct BrowserBackendSurface *src) {
    if (dst == NULL || src == NULL) { return; }
    dst->pixels = (uint8_t *) src->pixels;
    dst->width = src->width;
    dst->height = src->height;
    dst->surfaceWidth = src->surfaceWidth;
    dst->surfaceHeight = src->surfaceHeight;
}

static void browser_backend_unload_module(void) {
    sBrowserBackendApi = NULL;
    if (sBrowserBackendModule != NULL) {
        FreeLibrary(sBrowserBackendModule);
        sBrowserBackendModule = NULL;
    }
}

static bool browser_backend_load_module(void) {
    char exePath[MAX_PATH] = { 0 };
    char exeDir[MAX_PATH] = { 0 };
    char dllPath[MAX_PATH] = { 0 };
    char *lastSlash = NULL;

    if (GetModuleFileNameA(NULL, exePath, (DWORD) sizeof(exePath)) == 0) {
        exePath[0] = '\0';
    }

    lastSlash = strrchr(exePath, '\\');
    if (lastSlash != NULL) {
        *lastSlash = '\0';
        snprintf(exeDir, sizeof(exeDir), "%s", exePath);

        if (snprintf(dllPath, sizeof(dllPath), "%s\\%s\\%s", exeDir, BROWSER_BACKEND_RUNTIME_DIR, BROWSER_BACKEND_DLL_NAME) >= 0) {
            sBrowserBackendModule = LoadLibraryExA(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        }

        if (sBrowserBackendModule == NULL && snprintf(dllPath, sizeof(dllPath), "%s\\%s", exeDir, BROWSER_BACKEND_DLL_NAME) >= 0) {
            sBrowserBackendModule = LoadLibraryExA(dllPath, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        }
    }

    if (sBrowserBackendModule == NULL) {
        sBrowserBackendModule = LoadLibraryA(BROWSER_BACKEND_DLL_NAME);
    }

    if (sBrowserBackendModule == NULL) {
        LOG_INFO("Browser backend bridge '%s' is not present. Browser APIs will stay unavailable.", BROWSER_BACKEND_DLL_NAME);
        return false;
    }

    BrowserBackendGetApiFn getApi = (BrowserBackendGetApiFn) GetProcAddress(sBrowserBackendModule, BROWSER_BACKEND_GET_API_NAME);
    if (getApi == NULL) {
        LOG_ERROR("Browser backend bridge '%s' does not export %s.", BROWSER_BACKEND_DLL_NAME, BROWSER_BACKEND_GET_API_NAME);
        browser_backend_unload_module();
        return false;
    }

    sBrowserBackendApi = getApi(BROWSER_BACKEND_API_VERSION);
    if (sBrowserBackendApi == NULL || sBrowserBackendApi->apiVersion != BROWSER_BACKEND_API_VERSION) {
        LOG_ERROR("Browser backend bridge '%s' returned an incompatible API table.", BROWSER_BACKEND_DLL_NAME);
        browser_backend_unload_module();
        return false;
    }

    return true;
}

bool browser_backend_init(void) {
    if (sBrowserBackendInited) {
        return true;
    }

    sBrowserBackendInited = true;
    sBrowserBackendAvailable = false;

    if (!browser_backend_load_module()) {
        return true;
    }

    if (sBrowserBackendApi->init == NULL || sBrowserBackendApi->available == NULL) {
        LOG_ERROR("Browser backend bridge '%s' is missing required entry points.", BROWSER_BACKEND_DLL_NAME);
        browser_backend_unload_module();
        return true;
    }

    const struct BrowserBackendApiCallbacks callbacks = {
        .notifyLoad = browser_backend_notify_load,
        .notifyError = browser_backend_notify_error,
        .notifyConsole = browser_backend_notify_console,
        .notifyMessage = browser_backend_notify_message,
        .notifyFunctionCall = browser_backend_notify_function_call,
        .notifyPaint = browser_backend_notify_paint,
        .notifyJsResult = browser_backend_notify_js_result,
        .resolveUrlToPath = browser_backend_resolve_url_to_path,
        .shouldLoadUrl = browser_backend_should_load_url,
    };

    if (!sBrowserBackendApi->init(&callbacks)) {
        LOG_ERROR("Browser backend bridge '%s' failed to initialize.", BROWSER_BACKEND_DLL_NAME);
        browser_backend_unload_module();
        return true;
    }

    sBrowserBackendAvailable = sBrowserBackendApi->available() != 0;
    if (!sBrowserBackendAvailable) {
        LOG_INFO("Browser backend bridge '%s' initialized but reported unavailable.", BROWSER_BACKEND_DLL_NAME);
    }
    return true;
}

void browser_backend_tick(void) {
    if (!sBrowserBackendAvailable || sBrowserBackendApi == NULL || sBrowserBackendApi->tick == NULL) { return; }
    sBrowserBackendApi->tick();
}

void browser_backend_shutdown(void) {
    if (!sBrowserBackendInited) { return; }

    if (sBrowserBackendApi != NULL && sBrowserBackendApi->shutdown != NULL) {
        sBrowserBackendApi->shutdown();
    }

    sBrowserBackendAvailable = false;
    sBrowserBackendInited = false;
    browser_backend_unload_module();
}

bool browser_backend_available(void) {
    return sBrowserBackendAvailable;
}

struct BrowserBackendBrowser *browser_backend_create(struct BrowserBackendSurface *surface, const struct BrowserBackendCreateParams *params) {
    if (!sBrowserBackendAvailable || sBrowserBackendApi == NULL || sBrowserBackendApi->create == NULL || surface == NULL || params == NULL) {
        return NULL;
    }

    struct BrowserBackendBrowser *browser = calloc(1, sizeof(*browser));
    if (browser == NULL) {
        LOG_ERROR("Failed to allocate browser backend handle.");
        return NULL;
    }

    struct BrowserBackendApiSurface apiSurface = { 0 };
    struct BrowserBackendApiCreateParams apiParams = {
        .browserId = params->browserId,
        .transparent = params->transparent ? 1 : 0,
        .audio = params->audio ? 1 : 0,
    };

    browser_backend_fill_api_surface(&apiSurface, surface);
    browser->handle = sBrowserBackendApi->create(&apiSurface, &apiParams);
    if (browser->handle == NULL) {
        free(browser);
        return NULL;
    }
    return browser;
}

void browser_backend_destroy(struct BrowserBackendBrowser *browser) {
    if (browser == NULL) { return; }
    if (sBrowserBackendApi != NULL && sBrowserBackendApi->destroy != NULL && browser->handle != NULL) {
        sBrowserBackendApi->destroy(browser->handle);
    }
    free(browser);
}

bool browser_backend_open_url(struct BrowserBackendBrowser *browser, const char *url) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->openUrl != NULL
        && sBrowserBackendApi->openUrl(browser->handle, url) != 0;
}

bool browser_backend_reload(struct BrowserBackendBrowser *browser) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->reload != NULL
        && sBrowserBackendApi->reload(browser->handle) != 0;
}

bool browser_backend_stop(struct BrowserBackendBrowser *browser) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->stop != NULL
        && sBrowserBackendApi->stop(browser->handle) != 0;
}

bool browser_backend_go_back(struct BrowserBackendBrowser *browser) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->goBack != NULL
        && sBrowserBackendApi->goBack(browser->handle) != 0;
}

bool browser_backend_go_forward(struct BrowserBackendBrowser *browser) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->goForward != NULL
        && sBrowserBackendApi->goForward(browser->handle) != 0;
}

bool browser_backend_run_js(struct BrowserBackendBrowser *browser, const char *code, s32 requestId) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->runJs != NULL
        && sBrowserBackendApi->runJs(browser->handle, code, requestId) != 0;
}

bool browser_backend_add_function(struct BrowserBackendBrowser *browser, s32 bindingId, const char *objectName, const char *functionName) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->addFunction != NULL
        && sBrowserBackendApi->addFunction(browser->handle, bindingId, objectName, functionName) != 0;
}

bool browser_backend_set_volume(struct BrowserBackendBrowser *browser, f32 volume) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->setVolume != NULL
        && sBrowserBackendApi->setVolume(browser->handle, volume) != 0;
}

bool browser_backend_set_muted(struct BrowserBackendBrowser *browser, bool muted) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->setMuted != NULL
        && sBrowserBackendApi->setMuted(browser->handle, muted ? 1 : 0) != 0;
}

bool browser_backend_resize(struct BrowserBackendBrowser *browser, struct BrowserBackendSurface *surface) {
    if (!sBrowserBackendAvailable || browser == NULL || browser->handle == NULL || sBrowserBackendApi->resize == NULL || surface == NULL) {
        return false;
    }

    struct BrowserBackendApiSurface apiSurface = { 0 };
    browser_backend_fill_api_surface(&apiSurface, surface);
    return sBrowserBackendApi->resize(browser->handle, &apiSurface) != 0;
}

bool browser_backend_send_mouse_move(struct BrowserBackendBrowser *browser, s32 x, s32 y, u32 modifiers) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->sendMouseMove != NULL
        && sBrowserBackendApi->sendMouseMove(browser->handle, x, y, modifiers) != 0;
}

bool browser_backend_send_mouse_button(struct BrowserBackendBrowser *browser, s32 x, s32 y, s32 button, bool down, s32 clickCount, u32 modifiers) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->sendMouseButton != NULL
        && sBrowserBackendApi->sendMouseButton(browser->handle, x, y, button, down ? 1 : 0, clickCount, modifiers) != 0;
}

bool browser_backend_send_mouse_wheel(struct BrowserBackendBrowser *browser, s32 x, s32 y, s32 deltaX, s32 deltaY, u32 modifiers) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->sendMouseWheel != NULL
        && sBrowserBackendApi->sendMouseWheel(browser->handle, x, y, deltaX, deltaY, modifiers) != 0;
}

bool browser_backend_send_key(struct BrowserBackendBrowser *browser, s32 eventType, s32 keyCode, s32 nativeCode, u32 modifiers, const char *text) {
    return sBrowserBackendAvailable && browser != NULL && browser->handle != NULL && sBrowserBackendApi->sendKey != NULL
        && sBrowserBackendApi->sendKey(browser->handle, eventType, keyCode, nativeCode, modifiers, text) != 0;
}

#endif
