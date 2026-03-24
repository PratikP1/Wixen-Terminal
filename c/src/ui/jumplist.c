/* jumplist.c — Windows taskbar jumplist using ICustomDestinationList COM */
#ifdef _WIN32

#include "wixen/ui/jumplist.h"
#include <windows.h>
#include <shlobj.h>
#include <propkey.h>
/* propvarutil.h may not declare InitPropVariantFromString in C mode.
 * Declare it manually. */
HRESULT InitPropVariantFromString(PCWSTR psz, PROPVARIANT *ppropvar);

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "propsys.lib")

bool wixen_jumplist_update(const wchar_t *exe_path,
                            const wchar_t **profile_names,
                            size_t profile_count) {
    HRESULT hr;
    ICustomDestinationList *cdl = NULL;
    IObjectCollection *collection = NULL;
    IObjectArray *removed = NULL;

    hr = CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER,
                           &IID_ICustomDestinationList, (void **)&cdl);
    if (FAILED(hr)) return false;

    UINT min_slots;
    hr = cdl->lpVtbl->BeginList(cdl, &min_slots, &IID_IObjectArray, (void **)&removed);
    if (FAILED(hr)) goto fail;

    hr = CoCreateInstance(&CLSID_EnumerableObjectCollection, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IObjectCollection, (void **)&collection);
    if (FAILED(hr)) goto fail;

    for (size_t i = 0; i < profile_count && i < min_slots; i++) {
        IShellLinkW *link = NULL;
        hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IShellLinkW, (void **)&link);
        if (FAILED(hr)) continue;

        link->lpVtbl->SetPath(link, exe_path);

        wchar_t args[256];
        _snwprintf_s(args, 256, _TRUNCATE, L"--profile \"%s\"", profile_names[i]);
        link->lpVtbl->SetArguments(link, args);
        link->lpVtbl->SetDescription(link, profile_names[i]);

        /* Set display name via IPropertyStore */
        IPropertyStore *ps = NULL;
        hr = link->lpVtbl->QueryInterface(link, &IID_IPropertyStore, (void **)&ps);
        if (SUCCEEDED(hr) && ps) {
            PROPVARIANT pv;
            InitPropVariantFromString(profile_names[i], &pv);
            ps->lpVtbl->SetValue(ps, &PKEY_Title, &pv);
            PropVariantClear(&pv);
            ps->lpVtbl->Commit(ps);
            ps->lpVtbl->Release(ps);
        }

        collection->lpVtbl->AddObject(collection, (IUnknown *)link);
        link->lpVtbl->Release(link);
    }

    IObjectArray *tasks_array = NULL;
    collection->lpVtbl->QueryInterface(collection, &IID_IObjectArray, (void **)&tasks_array);
    if (tasks_array) {
        cdl->lpVtbl->AddUserTasks(cdl, tasks_array);
        tasks_array->lpVtbl->Release(tasks_array);
    }

    cdl->lpVtbl->CommitList(cdl);

fail:
    if (removed) removed->lpVtbl->Release(removed);
    if (collection) collection->lpVtbl->Release(collection);
    if (cdl) cdl->lpVtbl->Release(cdl);
    return SUCCEEDED(hr);
}

bool wixen_jumplist_clear(void) {
    ICustomDestinationList *cdl = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_DestinationList, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_ICustomDestinationList, (void **)&cdl);
    if (FAILED(hr)) return false;
    cdl->lpVtbl->DeleteList(cdl, NULL);
    cdl->lpVtbl->Release(cdl);
    return true;
}

#endif /* _WIN32 */
