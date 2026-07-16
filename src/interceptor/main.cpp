#include <windows.h>
#include "mapi_impl.h"
#include "mapi_types.h"

// Forward exports - these will be called through the .def file
extern "C" {

ULONG STDAPICALLTYPE MAPISendMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return go_mapi::MapiImpl::MAPISendMailA(lhSession, ulUIParam, lpMessage, flFlags, ulReserved);
}

ULONG STDAPICALLTYPE MAPISendMailW(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessageW lpMessage,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return go_mapi::MapiImpl::MAPISendMailW(lhSession, ulUIParam, lpMessage, flFlags, ulReserved);
}

ULONG STDAPICALLTYPE MAPILogon(
    ULONG_PTR ulUIParam,
    LPSTR lpszProfileName,
    LPSTR lpszPassword,
    FLAGS flFlags,
    ULONG ulReserved,
    LPLHANDLE lphSession
) {
    return go_mapi::MapiImpl::MAPILogon(ulUIParam, lpszProfileName, lpszPassword, flFlags, ulReserved, lphSession);
}

ULONG STDAPICALLTYPE MAPILogoff(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return go_mapi::MapiImpl::MAPILogoff(lhSession, ulUIParam, flFlags, ulReserved);
}

ULONG STDAPICALLTYPE MAPIFreeBuffer(LPVOID pv) {
    return go_mapi::MapiImpl::MAPIFreeBuffer(pv);
}

ULONG STDAPICALLTYPE MAPIFindNext(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageType,
    LPSTR lpszSeedMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
) {
    return go_mapi::MapiImpl::MAPIFindNext(
        lhSession, ulUIParam, lpszMessageType, lpszSeedMessageID,
        flFlags, ulReserved, lpszMessageID);
}

ULONG STDAPICALLTYPE MAPIReadMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved,
    LPMapiMessage* lppMessage
) {
    return go_mapi::MapiImpl::MAPIReadMail(
        lhSession, ulUIParam, lpszMessageID, flFlags, ulReserved, lppMessage);
}

ULONG STDAPICALLTYPE MAPISaveMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiMessage lpMessage,
    FLAGS flFlags,
    ULONG ulReserved,
    LPSTR lpszMessageID
) {
    return go_mapi::MapiImpl::MAPISaveMail(
        lhSession, ulUIParam, lpMessage, flFlags, ulReserved, lpszMessageID);
}

ULONG STDAPICALLTYPE MAPIDeleteMail(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszMessageID,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return go_mapi::MapiImpl::MAPIDeleteMail(
        lhSession, ulUIParam, lpszMessageID, flFlags, ulReserved);
}

ULONG STDAPICALLTYPE MAPIAddress(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszCaption,
    ULONG nEditFields,
    LPSTR lpszLabels,
    ULONG nRecips,
    LPMapiRecipDesc lpRecips,
    FLAGS flFlags,
    ULONG ulReserved,
    ULONG* lpnNewRecips,
    LPMapiRecipDesc* lppNewRecips
) {
    return go_mapi::MapiImpl::MAPIAddress(
        lhSession, ulUIParam, lpszCaption, nEditFields, lpszLabels,
        nRecips, lpRecips, flFlags, ulReserved, lpnNewRecips, lppNewRecips);
}

ULONG STDAPICALLTYPE MAPIDetails(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPMapiRecipDesc lpRecip,
    FLAGS flFlags,
    ULONG ulReserved
) {
    return go_mapi::MapiImpl::MAPIDetails(
        lhSession, ulUIParam, lpRecip, flFlags, ulReserved);
}

ULONG STDAPICALLTYPE MAPIResolveName(
    LHANDLE lhSession,
    ULONG_PTR ulUIParam,
    LPSTR lpszName,
    FLAGS flFlags,
    ULONG ulReserved,
    LPMapiRecipDesc* lppRecip
) {
    return go_mapi::MapiImpl::MAPIResolveName(
        lhSession, ulUIParam, lpszName, flFlags, ulReserved, lppRecip);
}

ULONG STDAPICALLTYPE MAPISendDocuments(
    ULONG_PTR ulUIParam,
    LPSTR lpszDelimChar,
    LPSTR lpszFilePaths,
    LPSTR lpszFileNames,
    ULONG ulReserved
) {
    return go_mapi::MapiImpl::MAPISendDocuments(ulUIParam, lpszDelimChar, lpszFilePaths, lpszFileNames, ulReserved);
}

}  // extern "C"

// DllMain runs under the Windows loader lock. Keep it inert; all filesystem
// and runtime initialization is performed lazily by exported MAPI calls.
BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
    return TRUE;
}
