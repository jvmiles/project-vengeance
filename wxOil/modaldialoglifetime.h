// SPDX-License-Identifier: GPL-2.0-only
// Keep managed modal windows alive until wxGTK ShowModal() has finished teardown.
#ifndef XARA_MODAL_DIALOG_LIFETIME_H
#define XARA_MODAL_DIALOG_LIFETIME_H

#include <wx/window.h>

class XaraModalDialogLifetime
{
public:
    explicit XaraModalDialogLifetime(wxWindow* window)
        : m_window(window), m_previous(Active()), m_destroy(false)
    {
        Active() = this;
    }
    ~XaraModalDialogLifetime()
    {
        wxASSERT(Active() == this);
        Active() = m_previous;
        if (m_destroy)
            m_window->Destroy();
    }
    // Only windows whose ShowModal call is owned by DialogManager are deferred.
    // Nested modal calls have independent lifetimes; no DialogOp pointer is kept.
    static bool DeferDestroy(wxWindow* window)
    {
        for (XaraModalDialogLifetime* scope = Active(); scope; scope = scope->m_previous)
            if (scope->m_window == window)
            {
                scope->m_destroy = true;
                return true;
            }
        return false;
    }
private:
    static XaraModalDialogLifetime*& Active()
    {
        static XaraModalDialogLifetime* active = NULL;
        return active;
    }
    XaraModalDialogLifetime(const XaraModalDialogLifetime&);
    XaraModalDialogLifetime& operator=(const XaraModalDialogLifetime&);
    wxWindow* m_window;
    XaraModalDialogLifetime* m_previous;
    bool m_destroy;
};
#endif
