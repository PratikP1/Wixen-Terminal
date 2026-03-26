/* jumplist.c — Windows taskbar jumplist using ICustomDestinationList COM */
#ifdef _WIN32

#include "wixen/ui/jumplist.h"
#include <windows.h>
#include <shlobj.h>
#include <propkey.h>
/* propvarutil.h does not declare InitPropVariantFromString in C mode.
 * Implement inline — avoids unresolved symbol with propsys.lib in
 * static-lib-to-test linking scenarios. */
static HRESULT wixen_init_propvariant_from_string(PCWSTR psz, PROPVARIANT *ppropvar) {
    PropVariantInit(ppropvar);
    size_t len = wcslen(psz) + 1;
    ppropvar->vt = VT_LPWSTR;
    ppropvar->pwszVal = (LPWSTR)CoTaskMemAlloc(len * sizeof(WCHAR));
    if (!ppropvar->pwszVal) return E_OUTOFMEMORY;
    memcpy(ppropvar->pwszVal, psz, len * sizeof(WCHAR));
    return S_OK;
}

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

/* Ensure COM is initialized for the calling thread.
 * Returns true if COM is usable, false otherwise. */
static bool ensure_com_initialized(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (hr == S_OK || hr == S_FALSE) {
        /* S_OK = fresh init, S_FALSE = already initialized on this thread.
         * Both mean COM is usable. */
        return true;
    }
    if (hr == RPC_E_CHANGED_MODE) {
        /* COM already initialized with different threading model.
         * That's fine — COM is still usable. */
        return true;
    }
    return false;
}

bool wixen_jumplist_update(const wchar_t *exe_path,
                            const wchar_t **profile_names,
                            size_t profile_count) {
    /* Validate inputs */
    if (!exe_path || exe_path[0] == L'\0') return false;
    if (!profile_names) return false;
    if (profile_count == 0) return false;

    if (!ensure_com_initialized()) return false;

    HRESULT hr;
    ICustomDestinationList *cdl = NULL;
    IObjectCollection *collection = NULL;
    IObjectArray *removed = NULL;
    bool success = false;

    hr = CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER,
                           &IID_ICustomDestinationList, (void **)&cdl);
    if (FAILED(hr)) return false;

    UINT min_slots;
    hr = cdl->lpVtbl->BeginList(cdl, &min_slots, &IID_IObjectArray, (void **)&removed);
    if (FAILED(hr)) goto cleanup;

    hr = CoCreateInstance(&CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IObjectCollection, (void **)&collection);
    if (FAILED(hr)) goto cleanup;

    for (size_t i = 0; i < profile_count && i < min_slots; i++) {
        /* Skip NULL entries in the profile list */
        if (!profile_names[i]) continue;

        IShellLinkW *link = NULL;
        hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IShellLinkW, (void **)&link);
        if (FAILED(hr)) continue;

        hr = link->lpVtbl->SetPath(link, exe_path);
        if (FAILED(hr)) { link->lpVtbl->Release(link); continue; }

        wchar_t args[256];
        _snwprintf_s(args, 256, _TRUNCATE, L"--profile \"%s\"", profile_names[i]);
        hr = link->lpVtbl->SetArguments(link, args);
        if (FAILED(hr)) { link->lpVtbl->Release(link); continue; }

        hr = link->lpVtbl->SetDescription(link, profile_names[i]);
        if (FAILED(hr)) { link->lpVtbl->Release(link); continue; }

        /* Set display name via IPropertyStore */
        IPropertyStore *ps = NULL;
        hr = link->lpVtbl->QueryInterface(link, &IID_IPropertyStore, (void **)&ps);
        if (SUCCEEDED(hr) && ps) {
            PROPVARIANT pv;
            hr = wixen_init_propvariant_from_string(profile_names[i], &pv);
            if (SUCCEEDED(hr)) {
                ps->lpVtbl->SetValue(ps, &PKEY_Title, &pv);
                PropVariantClear(&pv);
                ps->lpVtbl->Commit(ps);
            }
            ps->lpVtbl->Release(ps);
        }

        hr = collection->lpVtbl->AddObject(collection, (IUnknown *)link);
        link->lpVtbl->Release(link);
        if (FAILED(hr)) goto cleanup;
    }

    {
        IObjectArray *tasks_array = NULL;
        hr = collection->lpVtbl->QueryInterface(collection, &IID_IObjectArray,
                                                  (void **)&tasks_array);
        if (FAILED(hr) || !tasks_array) goto cleanup;

        hr = cdl->lpVtbl->AddUserTasks(cdl, tasks_array);
        tasks_array->lpVtbl->Release(tasks_array);
        if (FAILED(hr)) goto cleanup;
    }

    hr = cdl->lpVtbl->CommitList(cdl);
    if (SUCCEEDED(hr)) success = true;

cleanup:
    if (removed) removed->lpVtbl->Release(removed);
    if (collection) collection->lpVtbl->Release(collection);
    if (cdl) cdl->lpVtbl->Release(cdl);
    return success;
}

bool wixen_jumplist_clear(void) {
    if (!ensure_com_initialized()) return false;

    ICustomDestinationList *cdl = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ICustomDestinationList, (void **)&cdl);
    if (FAILED(hr)) return false;

    hr = cdl->lpVtbl->DeleteList(cdl, NULL);
    cdl->lpVtbl->Release(cdl);
    return SUCCEEDED(hr);
}

#endif /* _WIN32 */
