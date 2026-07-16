#pragma once

#include "mapi_types.h"
#include "json_writer.h"

namespace go_mapi {

class MapiImpl {
public:
    // ANSI version of MAPISendMail
    static ULONG MAPISendMailA(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPMapiMessage lpMessage,
        FLAGS flFlags,
        ULONG ulReserved
    );

    // Unicode version of MAPISendMail
    static ULONG MAPISendMailW(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPMapiMessageW lpMessage,
        FLAGS flFlags,
        ULONG ulReserved
    );

    // Stub implementations
    static ULONG MAPILogon(
        ULONG_PTR ulUIParam,
        LPSTR lpszProfileName,
        LPSTR lpszPassword,
        FLAGS flFlags,
        ULONG ulReserved,
        LPLHANDLE lphSession
    );

    static ULONG MAPILogoff(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        FLAGS flFlags,
        ULONG ulReserved
    );

    static ULONG MAPIFreeBuffer(LPVOID pv);

    static ULONG MAPIFindNext(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPSTR lpszMessageType,
        LPSTR lpszSeedMessageID,
        FLAGS flFlags,
        ULONG ulReserved,
        LPSTR lpszMessageID
    );

    static ULONG MAPIReadMail(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPSTR lpszMessageID,
        FLAGS flFlags,
        ULONG ulReserved,
        LPMapiMessage* lppMessage
    );

    static ULONG MAPISaveMail(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPMapiMessage lpMessage,
        FLAGS flFlags,
        ULONG ulReserved,
        LPSTR lpszMessageID
    );

    static ULONG MAPIDeleteMail(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPSTR lpszMessageID,
        FLAGS flFlags,
        ULONG ulReserved
    );

    static ULONG MAPIAddress(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPSTR lpszCaption,
        ULONG nEditFields,
        LPSTR lpszLabels,
        ULONG nRecips,
        LPMapiRecipDesc lpRecips,
        FLAGS flFlags,
        ULONG ulReserved,
        LPULONG lpnNewRecips,
        LPMapiRecipDesc* lppNewRecips
    );

    static ULONG MAPIDetails(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPMapiRecipDesc lpRecip,
        FLAGS flFlags,
        ULONG ulReserved
    );

    static ULONG MAPIResolveName(
        LHANDLE lhSession,
        ULONG_PTR ulUIParam,
        LPSTR lpszName,
        FLAGS flFlags,
        ULONG ulReserved,
        LPMapiRecipDesc* lppRecip
    );

    static ULONG MAPISendDocuments(
        ULONG_PTR ulUIParam,
        LPSTR lpszDelimChar,
        LPSTR lpszFilePaths,
        LPSTR lpszFileNames,
        ULONG ulReserved
    );

private:
    // Get application name (for originApp field).
    // Kept in MapiImpl because it queries the live process via Windows APIs
    // (GetModuleFileNameExW) — not pure conversion, so excluded from message_converter.
    static std::string GetOriginApplicationName();
};

} // namespace go_mapi
