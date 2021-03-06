// simplewall
// Copyright (c) 2016-2020 Henry++

#include "global.hpp"

HFONT hfont_title = NULL;
HFONT hfont_link = NULL;
HFONT hfont_text = NULL;

VOID _app_notifycreatewindow ()
{
	config.hnotification = CreateDialog (app.GetHINSTANCE (), MAKEINTRESOURCE (IDD_NOTIFICATION), NULL, &NotificationProc);
}

BOOLEAN _app_notifycommand (HWND hwnd, INT button_id, time_t seconds)
{
	SIZE_T app_hash = _app_notifyget_id (hwnd, FALSE);
	PR_OBJECT ptr_app_object = _app_getappitem (app_hash);

	if (!ptr_app_object)
		return FALSE;

	PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

	if (!ptr_app)
	{
		_r_obj2_dereference (ptr_app_object);
		return FALSE;
	}

	_app_freenotify (app_hash, ptr_app);

	OBJECTS_VEC rules;

	INT listview_id = _app_getlistview_id (ptr_app->type);
	INT item_pos = _app_getposition (app.GetHWND (), listview_id, app_hash);

	if (button_id == IDC_ALLOW_BTN || button_id == IDC_BLOCK_BTN)
	{
		ptr_app->is_enabled = (button_id == IDC_ALLOW_BTN);
		ptr_app->is_silent = (button_id == IDC_BLOCK_BTN);

		if (ptr_app->is_enabled && seconds)
		{
			_app_timer_set (app.GetHWND (), ptr_app, seconds);
		}
		else
		{
			if (item_pos != INVALID_INT)
			{
				_r_fastlock_acquireshared (&lock_checkbox);
				_app_setappiteminfo (app.GetHWND (), listview_id, item_pos, app_hash, ptr_app);
				_r_fastlock_releaseshared (&lock_checkbox);
			}
		}

		rules.push_back (ptr_app_object);
	}
	else if (button_id == IDC_LATER_BTN)
	{
		// TODO: do somethig!!!
	}
	else if (button_id == IDM_DISABLENOTIFICATIONS)
	{
		ptr_app->is_silent = TRUE;
	}

	ptr_app->last_notify = _r_unixtime_now ();

	HANDLE hengine = _wfp_getenginehandle ();

	if (hengine)
		_wfp_create3filters (hengine, rules, __LINE__);

	_app_freeobjects_vec (rules);

	_app_refreshstatus (app.GetHWND (), listview_id);
	_app_profile_save (NULL);

	if (listview_id && (INT)_r_tab_getlparam (app.GetHWND (), IDC_TAB, INVALID_INT) == listview_id)
	{
		_app_listviewsort (app.GetHWND (), listview_id, INVALID_INT, FALSE);
		_r_listview_redraw (app.GetHWND (), listview_id, INVALID_INT);
	}

	return TRUE;
}

BOOLEAN _app_notifyadd (HWND hwnd, PITEM_LOG ptr_log, PITEM_APP ptr_app)
{
	// check for last display time
	time_t current_time = _r_unixtime_now ();
	time_t notification_timeout = app.ConfigGetLong64 (L"NotificationsTimeout", NOTIFY_TIMEOUT_DEFAULT);

	if (notification_timeout && ((current_time - ptr_app->last_notify) <= notification_timeout))
		return FALSE;

	ptr_app->last_notify = current_time;

	if (!ptr_log->hicon)
		_app_getappicon (ptr_app, FALSE, NULL, &ptr_log->hicon);

	// remove existing log item (if exists)
	if (ptr_app->pnotification)
	{
		_r_obj_dereference (ptr_app->pnotification);
		ptr_app->pnotification = NULL;
	}

	ptr_app->pnotification = (PITEM_LOG)_r_obj_reference (ptr_log);

	if (app.ConfigGetBoolean (L"IsNotificationsSound", TRUE))
		_app_notifyplaysound ();

	if (!_r_wnd_isundercursor (hwnd))
		_app_notifyshow (hwnd, ptr_log, TRUE, TRUE);

	return TRUE;
}

VOID _app_freenotify (SIZE_T app_hash, PITEM_APP ptr_app)
{
	HWND hwnd = config.hnotification;

	if (ptr_app)
	{
		if (ptr_app->pnotification)
		{
			_r_obj_dereference (ptr_app->pnotification);
			ptr_app->pnotification = NULL;
		}
	}

	if (_app_notifyget_id (hwnd, FALSE) == app_hash)
		_app_notifyget_id (hwnd, TRUE);

	_app_notifyrefresh (hwnd, TRUE);
}

SIZE_T _app_notifyget_id (HWND hwnd, BOOLEAN is_nearest)
{
	SIZE_T app_hash_current = (SIZE_T)GetWindowLongPtr (hwnd, GWLP_USERDATA);

	if (is_nearest)
	{
		for (auto &p : apps)
		{
			if (p.first == app_hash_current) // exclude current app from enumeration
				continue;

			PR_OBJECT ptr_app_object = _r_obj2_reference (p.second);

			if (!ptr_app_object)
				continue;

			PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

			if (ptr_app && ptr_app->pnotification)
			{
				_r_obj2_dereference (ptr_app_object);

				SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR)p.first);
				return p.first;
			}

			_r_obj2_dereference (ptr_app_object);
		}

		SetWindowLongPtr (hwnd, GWLP_USERDATA, 0);
		return 0;
	}

	return app_hash_current;
}

PITEM_LOG _app_notifyget_obj (SIZE_T app_hash)
{
	PR_OBJECT ptr_app_object = _app_getappitem (app_hash);

	if (ptr_app_object)
	{
		PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

		if (ptr_app)
		{
			PITEM_LOG ptr_log = (PITEM_LOG)_r_obj_reference (ptr_app->pnotification);

			_r_obj2_dereference (ptr_app_object);

			return ptr_log;
		}

		_r_obj2_dereference (ptr_app_object);
	}

	return NULL;
}

BOOLEAN _app_notifyshow (HWND hwnd, PITEM_LOG ptr_log, BOOLEAN is_forced, BOOLEAN is_safety)
{
	if (!app.ConfigGetBoolean (L"IsNotificationsEnabled", TRUE))
		return FALSE;

	PR_OBJECT ptr_app_object = _app_getappitem (ptr_log->app_hash);

	if (!ptr_app_object)
		return FALSE;

	PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

	if (!ptr_app)
	{
		_r_obj2_dereference (ptr_app_object);
		return FALSE;
	}

	rstring is_signed;
	rstring empty_text = app.LocaleString (IDS_STATUS_EMPTY, NULL);

	if (app.ConfigGetBoolean (L"IsCertificatesEnabled", FALSE))
	{
		if (ptr_app->is_signed)
		{
			PR_OBJECT ptr_signature_object = _app_getsignatureinfo (ptr_log->app_hash, ptr_app);

			if (ptr_signature_object)
			{
				if (ptr_signature_object->pdata)
					is_signed = (LPCWSTR)ptr_signature_object->pdata;

				else
					is_signed = app.LocaleString (IDS_SIGN_SIGNED, NULL);

				_r_obj2_dereference (ptr_signature_object);
			}
			else
			{
				is_signed = app.LocaleString (IDS_SIGN_SIGNED, NULL);
			}
		}
		else
		{
			is_signed = app.LocaleString (IDS_SIGN_UNSIGNED, NULL);
		}
	}

	SetWindowText (hwnd, _r_fmt (L"%s - " APP_NAME, app.LocaleString (IDS_NOTIFY_TITLE, NULL).GetString ()).GetString ());

	SetWindowLongPtr (hwnd, GWLP_USERDATA, (LONG_PTR)ptr_log->app_hash);
	SetWindowLongPtr (GetDlgItem (hwnd, IDC_HEADER_ID), GWLP_USERDATA, (LONG_PTR)ptr_log->hicon);

	// print table text
	{
		LPWSTR remote_fmt = NULL;
		_app_formataddress (ptr_log->af, 0, &ptr_log->remote_addr, 0, &remote_fmt, FMTADDR_RESOLVE_HOST);

		_r_ctrl_settabletext (hwnd, IDC_FILE_ID, app.LocaleString (IDS_NAME, L":").GetString (), IDC_FILE_TEXT, !_r_str_isempty (ptr_app->display_name) ? _r_path_getfilename (ptr_app->display_name) : empty_text.GetString ());
		_r_ctrl_settabletext (hwnd, IDC_SIGNATURE_ID, app.LocaleString (IDS_SIGNATURE, L":").GetString (), IDC_SIGNATURE_TEXT, is_signed.IsEmpty () ? empty_text.GetString () : is_signed.GetString ());
		_r_ctrl_settabletext (hwnd, IDC_ADDRESS_ID, app.LocaleString (IDS_ADDRESS, L":").GetString (), IDC_ADDRESS_TEXT, !_r_str_isempty (remote_fmt) ? remote_fmt : empty_text.GetString ());
		_r_ctrl_settabletext (hwnd, IDC_PORT_ID, app.LocaleString (IDS_PORT, L":").GetString (), IDC_PORT_TEXT, ptr_log->remote_port ? _app_formatport (ptr_log->remote_port, FALSE).GetString () : empty_text.GetString ());
		_r_ctrl_settabletext (hwnd, IDC_DIRECTION_ID, app.LocaleString (IDS_DIRECTION, L":").GetString (), IDC_DIRECTION_TEXT, _app_getdirectionname (ptr_log->direction, ptr_log->is_loopback, TRUE).GetString ());
		_r_ctrl_settabletext (hwnd, IDC_FILTER_ID, app.LocaleString (IDS_FILTER, L":").GetString (), IDC_FILTER_TEXT, !_r_str_isempty (ptr_log->filter_name) ? ptr_log->filter_name : empty_text.GetString ());
		_r_ctrl_settabletext (hwnd, IDC_DATE_ID, app.LocaleString (IDS_DATE, L":").GetString (), IDC_DATE_TEXT, _r_fmt_dateex (ptr_log->timestamp, FDTF_SHORTDATE | FDTF_LONGTIME).GetString ());

		SAFE_DELETE_MEMORY (remote_fmt);
	}

	_r_ctrl_settext (hwnd, IDC_RULES_BTN, app.LocaleString (IDS_TRAY_RULES, NULL).GetString ());
	_r_ctrl_settext (hwnd, IDC_ALLOW_BTN, app.LocaleString (IDS_ACTION_ALLOW, NULL).GetString ());
	_r_ctrl_settext (hwnd, IDC_BLOCK_BTN, app.LocaleString (IDS_ACTION_BLOCK, NULL).GetString ());

	_r_ctrl_enable (hwnd, IDC_RULES_BTN, !is_safety);
	_r_ctrl_enable (hwnd, IDC_ALLOW_BTN, !is_safety);
	_r_ctrl_enable (hwnd, IDC_BLOCK_BTN, !is_safety);
	_r_ctrl_enable (hwnd, IDC_LATER_BTN, !is_safety);

	if (is_safety)
		SetTimer (hwnd, NOTIFY_TIMER_SAFETY_ID, NOTIFY_TIMER_SAFETY_TIMEOUT, NULL);

	else
		KillTimer (hwnd, NOTIFY_TIMER_SAFETY_ID);

	_app_notifysetpos (hwnd, FALSE);

	// prevent fullscreen apps lose focus
	BOOLEAN is_fullscreenmode = _r_wnd_isfullscreenmode ();

	if (is_forced && is_fullscreenmode)
		is_forced = FALSE;

	InvalidateRect (GetDlgItem (hwnd, IDC_HEADER_ID), NULL, TRUE);
	InvalidateRect (hwnd, NULL, TRUE);

	_r_wnd_top (hwnd, !is_fullscreenmode);

	ShowWindow (hwnd, is_forced ? SW_SHOW : SW_SHOWNA);

	return TRUE;
}

VOID _app_notifyhide (HWND hwnd)
{
	ShowWindow (hwnd, SW_HIDE);
}

// Play notification sound even if system have "nosound" mode
VOID _app_notifyplaysound ()
{
	BOOLEAN result = FALSE;
	static WCHAR notify_snd_path[MAX_PATH] = {0};

	if (_r_str_isempty (notify_snd_path) || !_r_fs_exists (notify_snd_path))
	{
		notify_snd_path[0] = UNICODE_NULL;

#define NOTIFY_SOUND_NAME L"MailBeep"

		HKEY hkey;

		if (RegOpenKeyEx (HKEY_CURRENT_USER, L"AppEvents\\Schemes\\Apps\\.Default\\" NOTIFY_SOUND_NAME L"\\.Default", 0, KEY_READ, &hkey) == ERROR_SUCCESS)
		{
			rstring path = _r_reg_querystring (hkey, NULL);

			if (!path.IsEmpty ())
			{
				path = _r_path_expand (path.GetString ());

				if (_r_fs_exists (path.GetString ()))
				{
					_r_str_copy (notify_snd_path, RTL_NUMBER_OF (notify_snd_path), path.GetString ());
					result = TRUE;
				}
			}

			RegCloseKey (hkey);
		}
	}
	else
	{
		result = TRUE;
	}

	if (!result || !_r_fs_exists (notify_snd_path) || !PlaySound (notify_snd_path, NULL, SND_ASYNC | SND_NODEFAULT | SND_NOWAIT | SND_FILENAME | SND_SENTRY))
		PlaySound (NOTIFY_SOUND_NAME, NULL, SND_ASYNC | SND_NODEFAULT | SND_NOWAIT | SND_SENTRY);
}

VOID _app_notifyrefresh (HWND hwnd, BOOLEAN is_safety)
{
	if (!app.ConfigGetBoolean (L"IsNotificationsEnabled", TRUE) || !IsWindowVisible (hwnd))
	{
		_app_notifyhide (hwnd);
		return;
	}

	PITEM_LOG ptr_log = _app_notifyget_obj (_app_notifyget_id (hwnd, FALSE));

	if (!ptr_log)
	{
		_app_notifyhide (hwnd);
		return;
	}

	_app_notifyshow (hwnd, ptr_log, TRUE, is_safety);

	_r_obj_dereference (ptr_log);
}

VOID _app_notifysetpos (HWND hwnd, BOOLEAN is_forced)
{
	if (!is_forced && IsWindowVisible (hwnd))
	{
		RECT windowRect;

		if (GetWindowRect (hwnd, &windowRect))
		{
			_r_wnd_adjustwindowrect (hwnd, &windowRect);
			SetWindowPos (hwnd, NULL, windowRect.left, windowRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);

			return;
		}
	}

	BOOLEAN is_intray = app.ConfigGetBoolean (L"IsNotificationsOnTray", FALSE);

	if (is_intray)
	{
		RECT windowRect;

		if (GetWindowRect (hwnd, &windowRect))
		{
			RECT desktopRect;

			if (SystemParametersInfo (SPI_GETWORKAREA, 0, &desktopRect, 0))
			{
				APPBARDATA abd;
				abd.cbSize = sizeof (abd);

				if ((BOOL)SHAppBarMessage (ABM_GETTASKBARPOS, &abd))
				{
					INT border_x = _r_dc_getsystemmetrics (hwnd, SM_CXBORDER);

					if (abd.uEdge == ABE_LEFT)
					{
						windowRect.left = abd.rc.right + border_x;
						windowRect.top = (desktopRect.bottom - _r_calc_rectheight (LONG, &windowRect)) - border_x;
					}
					else if (abd.uEdge == ABE_TOP)
					{
						windowRect.left = (desktopRect.right - _r_calc_rectwidth (LONG, &windowRect)) - border_x;
						windowRect.top = abd.rc.bottom + border_x;
					}
					else if (abd.uEdge == ABE_RIGHT)
					{
						windowRect.left = (desktopRect.right - _r_calc_rectwidth (LONG, &windowRect)) - border_x;
						windowRect.top = (desktopRect.bottom - _r_calc_rectheight (LONG, &windowRect)) - border_x;
					}
					else/* if (abd.uEdge == ABE_BOTTOM)*/
					{
						windowRect.left = (desktopRect.right - (windowRect.right - windowRect.left)) - border_x;
						windowRect.top = (desktopRect.bottom - _r_calc_rectheight (LONG, &windowRect)) - border_x;
					}

					SetWindowPos (hwnd, NULL, windowRect.left, windowRect.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
					return;
				}
			}
		}
	}

	_r_wnd_center (hwnd, NULL); // display window on center (depends on error, config etc...)
}

HFONT _app_notifyfontinit (HWND hwnd, PLOGFONT plf, LONG height, LONG weight, LPCWSTR name, BOOLEAN is_underline)
{
	if (height)
		plf->lfHeight = _r_dc_fontsizetoheight (hwnd, height);

	plf->lfWeight = weight;
	plf->lfUnderline = is_underline;

	plf->lfCharSet = DEFAULT_CHARSET;
	plf->lfQuality = DEFAULT_QUALITY;

	if (!_r_str_isempty (name))
		_r_str_copy (plf->lfFaceName, LF_FACESIZE, name);

	return CreateFontIndirect (plf);
}

VOID _app_notifyfontset (HWND hwnd)
{
	SendMessage (hwnd, WM_SETICON, ICON_SMALL, (LPARAM)app.GetSharedImage (NULL, SIH_EXCLAMATION, _r_dc_getsystemmetrics (hwnd, SM_CXSMICON)));
	SendMessage (hwnd, WM_SETICON, ICON_BIG, (LPARAM)app.GetSharedImage (NULL, SIH_EXCLAMATION, _r_dc_getsystemmetrics (hwnd, SM_CXICON)));

	INT title_font_height = 12;
	INT text_font_height = 9;

	NONCLIENTMETRICS ncm = {0};
	ncm.cbSize = sizeof (ncm);

	if (SystemParametersInfo (SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0))
	{
		SAFE_DELETE_OBJECT (hfont_title);
		SAFE_DELETE_OBJECT (hfont_link);
		SAFE_DELETE_OBJECT (hfont_text);

		hfont_title = _app_notifyfontinit (hwnd, &ncm.lfCaptionFont, title_font_height, FW_NORMAL, UI_FONT, FALSE);
		hfont_link = _app_notifyfontinit (hwnd, &ncm.lfMessageFont, text_font_height, FW_NORMAL, UI_FONT, TRUE);
		hfont_text = _app_notifyfontinit (hwnd, &ncm.lfMessageFont, text_font_height, FW_NORMAL, UI_FONT, FALSE);

		SendDlgItemMessage (hwnd, IDC_HEADER_ID, WM_SETFONT, (WPARAM)hfont_title, TRUE);
		SendDlgItemMessage (hwnd, IDC_FILE_TEXT, WM_SETFONT, (WPARAM)hfont_link, TRUE);

		for (INT i = IDC_SIGNATURE_TEXT; i <= IDC_DATE_TEXT; i++)
			SendDlgItemMessage (hwnd, i, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);

		for (INT i = IDC_SIGNATURE_TEXT; i <= IDC_LATER_BTN; i++)
			SendDlgItemMessage (hwnd, i, WM_SETFONT, (WPARAM)hfont_text, TRUE);
	}

	// set button images
	SendDlgItemMessage (hwnd, IDC_RULES_BTN, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)config.hbmp_rules);
	SendDlgItemMessage (hwnd, IDC_ALLOW_BTN, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)config.hbmp_allow);
	SendDlgItemMessage (hwnd, IDC_BLOCK_BTN, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)config.hbmp_block);
	SendDlgItemMessage (hwnd, IDC_LATER_BTN, BM_SETIMAGE, IMAGE_BITMAP, (LPARAM)config.hbmp_cross);

	_r_ctrl_setbuttonmargins (hwnd, IDC_RULES_BTN);
	_r_ctrl_setbuttonmargins (hwnd, IDC_ALLOW_BTN);
	_r_ctrl_setbuttonmargins (hwnd, IDC_BLOCK_BTN);
	_r_ctrl_setbuttonmargins (hwnd, IDC_LATER_BTN);

	_r_wnd_addstyle (hwnd, IDC_RULES_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
	_r_wnd_addstyle (hwnd, IDC_ALLOW_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
	_r_wnd_addstyle (hwnd, IDC_BLOCK_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);
	_r_wnd_addstyle (hwnd, IDC_LATER_BTN, app.IsClassicUI () ? WS_EX_STATICEDGE : 0, WS_EX_STATICEDGE, GWL_EXSTYLE);

	InvalidateRect (hwnd, NULL, TRUE);
}

VOID DrawGradient (HDC hdc, const LPRECT lprc, COLORREF rgb1, COLORREF rgb2, ULONG mode)
{
	GRADIENT_RECT gradientRect = {0};
	TRIVERTEX triVertext[2] = {0};

	gradientRect.LowerRight = 1;

	triVertext[0].x = lprc->left - 1;
	triVertext[0].y = lprc->top - 1;
	triVertext[0].Red = GetRValue (rgb1) << 8;
	triVertext[0].Green = GetGValue (rgb1) << 8;
	triVertext[0].Blue = GetBValue (rgb1) << 8;

	triVertext[1].x = lprc->right;
	triVertext[1].y = lprc->bottom;
	triVertext[1].Red = GetRValue (rgb2) << 8;
	triVertext[1].Green = GetGValue (rgb2) << 8;
	triVertext[1].Blue = GetBValue (rgb2) << 8;

	GradientFill (hdc, triVertext, RTL_NUMBER_OF (triVertext), &gradientRect, 1, mode);
}

INT_PTR CALLBACK NotificationProc (HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_INITDIALOG:
		{
#if !defined(_APP_NO_DARKTHEME)
			_r_wnd_setdarktheme (hwnd);
#endif // !_APP_NO_DARKTHEME

			_app_notifyfontset (hwnd);

			HWND htip = _r_ctrl_createtip (hwnd);

			if (htip)
			{
				_r_ctrl_settip (htip, hwnd, IDC_FILE_TEXT, LPSTR_TEXTCALLBACK);
				_r_ctrl_settip (htip, hwnd, IDC_RULES_BTN, LPSTR_TEXTCALLBACK);
				_r_ctrl_settip (htip, hwnd, IDC_ALLOW_BTN, LPSTR_TEXTCALLBACK);
				_r_ctrl_settip (htip, hwnd, IDC_BLOCK_BTN, LPSTR_TEXTCALLBACK);
				_r_ctrl_settip (htip, hwnd, IDC_LATER_BTN, LPSTR_TEXTCALLBACK);
			}

			break;
		}

		case WM_NCCREATE:
		{
			_r_wnd_enablenonclientscaling (hwnd);
			break;
		}

		case WM_DPICHANGED:
		{
			_app_notifyfontset (hwnd);
			_app_notifyrefresh (hwnd, FALSE);

			break;
		}

		case WM_CLOSE:
		{
			_app_notifyhide (hwnd);

			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_ACTIVATE:
		{
			switch (wparam)
			{
				case WA_ACTIVE:
				case WA_CLICKACTIVE:
				{
					_r_wnd_top (hwnd, TRUE);
					SetActiveWindow (hwnd);
					break;
				}
			}

			break;
		}

		case WM_TIMER:
		{
			if (wparam == NOTIFY_TIMER_SAFETY_ID)
			{
				KillTimer (hwnd, wparam);

				_r_ctrl_enable (hwnd, IDC_RULES_BTN, TRUE);
				_r_ctrl_enable (hwnd, IDC_ALLOW_BTN, TRUE);
				_r_ctrl_enable (hwnd, IDC_BLOCK_BTN, TRUE);
				_r_ctrl_enable (hwnd, IDC_LATER_BTN, TRUE);
			}

			break;
		}

		case WM_SETTINGCHANGE:
		{
			_r_wnd_changesettings (hwnd, wparam, lparam);
			break;
		}

		case WM_ERASEBKGND:
		{
			RECT clientRect;

			if (GetClientRect (hwnd, &clientRect))
				_r_dc_fillrect ((HDC)wparam, &clientRect, GetSysColor (COLOR_WINDOW));

			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			HDC hdc = BeginPaint (hwnd, &ps);

			if (hdc)
			{
				RECT clientRect;

				if (GetClientRect (hwnd, &clientRect))
				{
					INT wnd_width = _r_calc_rectwidth (INT, &clientRect);
					INT wnd_height = _r_calc_rectheight (INT, &clientRect);

					SetRect (&clientRect, 0, wnd_height - _r_dc_getdpi (hwnd, _R_SIZE_FOOTERHEIGHT), wnd_width, wnd_height);
					_r_dc_fillrect (hdc, &clientRect, GetSysColor (COLOR_3DFACE));

					for (INT i = 0; i < wnd_width; i++)
						SetPixelV (hdc, i, clientRect.top, GetSysColor (COLOR_APPWORKSPACE));
				}

				EndPaint (hwnd, &ps);
			}

			break;
		}

		case WM_CTLCOLORDLG:
		{
			return (INT_PTR)GetSysColorBrush (COLOR_WINDOW);
		}

		case WM_CTLCOLORSTATIC:
		{
			HDC hdc = (HDC)wparam;
			INT ctrl_id = GetDlgCtrlID ((HWND)lparam);

			SetBkMode (hdc, TRANSPARENT); // HACK!!!

			if (ctrl_id == IDC_FILE_TEXT)
				SetTextColor (hdc, GetSysColor (COLOR_HIGHLIGHT));

			else
				SetTextColor (hdc, GetSysColor (COLOR_WINDOWTEXT));

			return (INT_PTR)GetSysColorBrush (COLOR_WINDOW);
		}

		case WM_DRAWITEM:
		{
			LPDRAWITEMSTRUCT drawInfo = (LPDRAWITEMSTRUCT)lparam;

			if (drawInfo->CtlID != IDC_HEADER_ID)
				break;

			INT wnd_icon_size = _r_dc_getsystemmetrics (hwnd, SM_CXICON);
			INT wnd_spacing = _r_dc_getdpi (hwnd, 12);

			RECT textRect;
			RECT iconRect;

			SetRect (&textRect, wnd_spacing, 0, _r_calc_rectwidth (INT, &drawInfo->rcItem) - (wnd_spacing * 3) - wnd_icon_size, _r_calc_rectheight (INT, &drawInfo->rcItem));
			SetRect (&iconRect, _r_calc_rectwidth (INT, &drawInfo->rcItem) - wnd_icon_size - wnd_spacing, (_r_calc_rectheight (INT, &drawInfo->rcItem) / 2) - (wnd_icon_size / 2), wnd_icon_size, wnd_icon_size);

			SetBkMode (drawInfo->hDC, TRANSPARENT);

			// draw background
			DrawGradient (drawInfo->hDC, &drawInfo->rcItem, app.ConfigGetUlong (L"NotificationBackground1", NOTIFY_GRADIENT_1), app.ConfigGetUlong (L"NotificationBackground2", NOTIFY_GRADIENT_2), GRADIENT_FILL_RECT_H);

			// draw title text
			WCHAR text[128] = {0};
			_r_str_printf (text, RTL_NUMBER_OF (text), app.LocaleString (IDS_NOTIFY_HEADER, NULL).GetString (), APP_NAME);

			COLORREF clr_prev = SetTextColor (drawInfo->hDC, GetSysColor (COLOR_WINDOW));
			DrawTextEx (drawInfo->hDC, text, (INT)_r_str_length (text), &textRect, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOCLIP | DT_NOPREFIX, NULL);
			SetTextColor (drawInfo->hDC, clr_prev);

			// draw icon
			HICON hicon = (HICON)GetWindowLongPtr (drawInfo->hwndItem, GWLP_USERDATA);

			if (!hicon)
				hicon = config.hicon_large;

			if (hicon)
				DrawIconEx (drawInfo->hDC, iconRect.left, iconRect.top, hicon, iconRect.right, iconRect.bottom, 0, NULL, DI_IMAGE | DI_MASK);

			SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
			return TRUE;
		}

		case WM_SETCURSOR:
		{
			INT ctrl_id = GetDlgCtrlID ((HWND)wparam);

			if (ctrl_id == IDC_FILE_TEXT)
			{
				SetCursor (LoadCursor (NULL, IDC_HAND));

				SetWindowLongPtr (hwnd, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}

			break;
		}

		case WM_LBUTTONDOWN:
		{
			PostMessage (hwnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
			break;
		}

		case WM_ENTERSIZEMOVE:
		case WM_EXITSIZEMOVE:
		case WM_CAPTURECHANGED:
		{
			LONG_PTR exstyle = GetWindowLongPtr (hwnd, GWL_EXSTYLE);

			if ((exstyle & WS_EX_LAYERED) == 0)
				SetWindowLongPtr (hwnd, GWL_EXSTYLE, exstyle | WS_EX_LAYERED);

			SetLayeredWindowAttributes (hwnd, 0, (msg == WM_ENTERSIZEMOVE) ? 100 : 255, LWA_ALPHA);
			SetCursor (LoadCursor (NULL, (msg == WM_ENTERSIZEMOVE) ? IDC_SIZEALL : IDC_ARROW));

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch (nmlp->code)
			{
				case BCN_DROPDOWN:
				{
					INT ctrl_id = PtrToInt ((PVOID)nmlp->idFrom);

					if (!_r_ctrl_isenabled (hwnd, ctrl_id) || (ctrl_id != IDC_RULES_BTN && ctrl_id != IDC_ALLOW_BTN))
						break;

					HMENU hsubmenu = CreatePopupMenu ();

					if (!hsubmenu)
						break;

					if (ctrl_id == IDC_RULES_BTN)
					{
						AppendMenu (hsubmenu, MF_STRING, IDM_DISABLENOTIFICATIONS, app.LocaleString (IDS_DISABLENOTIFICATIONS, NULL).GetString ());

						_app_generate_rulesmenu (hsubmenu, _app_notifyget_id (hwnd, FALSE));
					}
					else if (ctrl_id == IDC_ALLOW_BTN)
					{
						AppendMenu (hsubmenu, MF_STRING, IDC_ALLOW_BTN, app.LocaleString (IDS_ACTION_ALLOW, NULL).GetString ());
						AppendMenu (hsubmenu, MF_SEPARATOR, 0, NULL);

						_app_generate_timermenu (hsubmenu, 0);

						_r_menu_checkitem (hsubmenu, IDC_ALLOW_BTN, IDC_ALLOW_BTN, MF_BYCOMMAND, IDC_ALLOW_BTN);
					}

					RECT buttonRect;

					if (GetClientRect (nmlp->hwndFrom, &buttonRect))
					{
						ClientToScreen (nmlp->hwndFrom, (LPPOINT)&buttonRect);

						_r_wnd_adjustwindowrect (nmlp->hwndFrom, &buttonRect);
					}

					_r_menu_popup (hsubmenu, hwnd, (LPPOINT)&buttonRect, TRUE);

					DestroyMenu (hsubmenu);

					break;
				}

				case TTN_GETDISPINFO:
				{
					LPNMTTDISPINFO lpnmdi = (LPNMTTDISPINFO)lparam;

					if ((lpnmdi->uFlags & TTF_IDISHWND) == 0)
						break;

					WCHAR buffer[1024] = {0};
					INT ctrl_id = GetDlgCtrlID ((HWND)lpnmdi->hdr.idFrom);

					if (ctrl_id == IDC_FILE_TEXT)
					{
						PITEM_LOG ptr_log = _app_notifyget_obj (_app_notifyget_id (hwnd, FALSE));

						if (ptr_log)
						{
							INT listview_id = (INT)_app_getappinfo (ptr_log->app_hash, InfoListviewId);

							if (listview_id)
							{
								INT item_id = _app_getposition (app.GetHWND (), listview_id, ptr_log->app_hash);

								if (item_id != INVALID_INT)
								{
									NMLVGETINFOTIP nmlvgit = {0};

									nmlvgit.hdr.idFrom = (UINT_PTR)listview_id;
									nmlvgit.iItem = item_id;
									nmlvgit.lParam = 0;

									_r_str_copy (buffer, RTL_NUMBER_OF (buffer), _app_gettooltip (app.GetHWND (), &nmlvgit).GetString ());
								}
							}

							_r_obj_dereference (ptr_log);
						}
					}
					else if (ctrl_id == IDC_RULES_BTN)
					{
						_r_str_copy (buffer, RTL_NUMBER_OF (buffer), app.LocaleString (IDS_NOTIFY_TOOLTIP, NULL).GetString ());
					}
					else if (ctrl_id == IDC_ALLOW_BTN)
					{
						_r_str_copy (buffer, RTL_NUMBER_OF (buffer), app.LocaleString (IDS_ACTION_ALLOW_HINT, NULL).GetString ());
					}
					else if (ctrl_id == IDC_BLOCK_BTN)
					{
						_r_str_copy (buffer, RTL_NUMBER_OF (buffer), app.LocaleString (IDS_ACTION_BLOCK_HINT, NULL).GetString ());
					}
					else if (ctrl_id == IDC_LATER_BTN)
					{
						_r_str_copy (buffer, RTL_NUMBER_OF (buffer), app.LocaleString (IDS_ACTION_LATER_HINT, NULL).GetString ());
					}
					else
					{
						_r_str_copy (buffer, RTL_NUMBER_OF (buffer), _r_ctrl_gettext (hwnd, ctrl_id).GetString ());
					}

					if (!_r_str_isempty (buffer))
						lpnmdi->lpszText = buffer;

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			INT ctrl_id = LOWORD (wparam);

			if ((LOWORD (wparam) >= IDX_RULES_SPECIAL && LOWORD (wparam) <= IDX_RULES_SPECIAL + (INT)rules_arr.size ()))
			{
				SIZE_T rule_idx = (LOWORD (wparam) - IDX_RULES_SPECIAL);
				PR_OBJECT ptr_rule_object = _app_getrulebyid (rule_idx);

				if (!ptr_rule_object)
					return FALSE;

				PITEM_RULE ptr_rule = (PITEM_RULE)ptr_rule_object->pdata;

				if (ptr_rule)
				{
					PITEM_LOG ptr_log = _app_notifyget_obj (_app_notifyget_id (hwnd, FALSE));

					if (ptr_log)
					{
						if (ptr_log->app_hash && !(ptr_rule->is_forservices && (ptr_log->app_hash == config.ntoskrnl_hash || ptr_log->app_hash == config.svchost_hash)))
						{
							PR_OBJECT ptr_app_object = _app_getappitem (ptr_log->app_hash);

							if (ptr_app_object)
							{
								PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

								if (ptr_app)
								{
									_app_freenotify (ptr_log->app_hash, ptr_app);

									BOOLEAN is_remove = ptr_rule->is_enabled && (ptr_rule->apps.find (ptr_log->app_hash) != ptr_rule->apps.end ());

									if (is_remove)
									{
										ptr_rule->apps.erase (ptr_log->app_hash);

										if (ptr_rule->apps.empty ())
											_app_ruleenable (ptr_rule, FALSE);
									}
									else
									{
										ptr_rule->apps[ptr_log->app_hash] = TRUE;
										_app_ruleenable (ptr_rule, TRUE);
									}

									INT listview_id = (INT)_r_tab_getlparam (app.GetHWND (), IDC_TAB, INVALID_INT);
									INT app_listview_id = _app_getlistview_id (ptr_app->type);
									INT rule_listview_id = _app_getlistview_id (ptr_rule->type);

									{
										INT item_pos = _app_getposition (app.GetHWND (), app_listview_id, ptr_log->app_hash);

										if (item_pos != INVALID_INT)
										{
											_r_fastlock_acquireshared (&lock_checkbox);
											_app_setappiteminfo (app.GetHWND (), app_listview_id, item_pos, ptr_log->app_hash, ptr_app);
											_r_fastlock_releaseshared (&lock_checkbox);
										}
									}

									{
										INT item_pos = _app_getposition (app.GetHWND (), rule_listview_id, rule_idx);

										if (item_pos != INVALID_INT)
										{
											_r_fastlock_acquireshared (&lock_checkbox);
											_app_setruleiteminfo (app.GetHWND (), rule_listview_id, item_pos, ptr_rule, FALSE);
											_r_fastlock_releaseshared (&lock_checkbox);
										}
									}

									OBJECTS_VEC rules;
									rules.push_back (ptr_rule_object);

									HANDLE hengine = _wfp_getenginehandle ();

									if (hengine)
										_wfp_create4filters (hengine, rules, __LINE__);

									if (listview_id == app_listview_id || listview_id == rule_listview_id)
									{
										_app_listviewsort (app.GetHWND (), listview_id, INVALID_INT, FALSE);
										_r_listview_redraw (app.GetHWND (), listview_id, INVALID_INT);
									}

									_app_refreshstatus (app.GetHWND (), listview_id);
									_app_profile_save (NULL);
								}

								_r_obj2_dereference (ptr_app_object);
							}
						}

						_r_obj_dereference (ptr_log);
					}
				}

				_r_obj2_dereference (ptr_rule_object);

				return FALSE;
			}
			else if ((ctrl_id >= IDX_TIMER && ctrl_id <= IDX_TIMER + (INT)timers.size ()))
			{
				if (!_r_ctrl_isenabled (hwnd, IDC_ALLOW_BTN))
					return FALSE;

				SIZE_T timer_idx = (ctrl_id - IDX_TIMER);

				_app_notifycommand (hwnd, IDC_ALLOW_BTN, timers.at (timer_idx));

				return FALSE;
			}

			switch (ctrl_id)
			{
				case IDCANCEL: // process Esc key
				{
					_app_notifyhide (hwnd);
					break;
				}

				case IDC_FILE_TEXT:
				{
					PITEM_LOG ptr_log = _app_notifyget_obj (_app_notifyget_id (hwnd, FALSE));

					if (ptr_log)
					{
						PR_OBJECT ptr_app_object = _app_getappitem (ptr_log->app_hash);

						if (ptr_app_object)
						{
							PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

							if (ptr_app)
							{
								INT listview_id = _app_getlistview_id (ptr_app->type);

								if (listview_id)
									_app_showitem (app.GetHWND (), listview_id, _app_getposition (app.GetHWND (), listview_id, ptr_log->app_hash), INVALID_INT);

								_r_wnd_toggle (app.GetHWND (), TRUE);
							}

							_r_obj2_dereference (ptr_app_object);
						}

						_r_obj_dereference (ptr_log);
					}

					break;
				}

				case IDC_RULES_BTN:
				{
					if (_r_ctrl_isenabled (hwnd, ctrl_id))
					{
						// HACK!!!
						NMHDR hdr = {0};

						hdr.code = BCN_DROPDOWN;
						hdr.idFrom = (UINT_PTR)ctrl_id;
						hdr.hwndFrom = GetDlgItem (hwnd, ctrl_id);

						SendMessage (hwnd, WM_NOTIFY, TRUE, (LPARAM)&hdr);
					}

					break;
				}

				case IDC_ALLOW_BTN:
				case IDC_BLOCK_BTN:
				case IDC_LATER_BTN:
				{
					if (_r_ctrl_isenabled (hwnd, ctrl_id))
						_app_notifycommand (hwnd, ctrl_id, 0);

					break;
				}

				case IDM_DISABLENOTIFICATIONS:
				{
					if (_r_ctrl_isenabled (hwnd, IDC_RULES_BTN))
						_app_notifycommand (hwnd, ctrl_id, 0);

					break;
				}

				case IDM_EDITRULES:
				{
					_r_wnd_toggle (app.GetHWND (), TRUE);
					_app_settab_id (app.GetHWND (), _app_getlistview_id (DataRuleCustom));

					break;
				}

				case IDM_OPENRULESEDITOR:
				{
					PITEM_RULE ptr_rule = new ITEM_RULE;
					PR_OBJECT ptr_rule_object = _r_obj2_allocateex (ptr_rule, &_app_dereferencerule);

					SIZE_T app_hash = 0;
					PITEM_LOG ptr_log = _app_notifyget_obj (_app_notifyget_id (hwnd, FALSE));

					if (ptr_log)
					{
						app_hash = ptr_log->app_hash;

						ptr_rule->apps[app_hash] = TRUE;
						ptr_rule->protocol = ptr_log->protocol;
						ptr_rule->direction = ptr_log->direction;

						PR_OBJECT ptr_app_object = _app_getappitem (app_hash);

						if (ptr_app_object)
						{
							PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

							if (ptr_app)
								_app_freenotify (app_hash, ptr_app);

							_r_obj2_dereference (ptr_app_object);
						}

						LPWSTR rule = NULL;

						if (_app_formataddress (ptr_log->af, 0, &ptr_log->remote_addr, ptr_log->remote_port, &rule, FMTADDR_AS_RULE))
						{
							SIZE_T len = _r_str_length (rule);

							_r_str_alloc (&ptr_rule->pname, len, rule);
							_r_str_alloc (&ptr_rule->prule_remote, len, rule);
						}

						SAFE_DELETE_MEMORY (rule);

						_r_obj_dereference (ptr_log);
					}
					else
					{
						_r_obj2_dereference (ptr_rule_object);
						break;
					}

					ptr_rule->type = DataRuleCustom;
					ptr_rule->is_block = FALSE;

					_app_ruleenable (ptr_rule, TRUE);

					if (DialogBoxParam (NULL, MAKEINTRESOURCE (IDD_EDITOR), app.GetHWND (), &EditorProc, (LPARAM)ptr_rule_object))
					{
						SIZE_T rule_idx = rules_arr.size ();
						rules_arr.push_back (_r_obj2_reference (ptr_rule_object));

						INT listview_id = (INT)_r_tab_getlparam (app.GetHWND (), IDC_TAB, INVALID_INT);

						// set rule information
						INT rules_listview_id = _app_getlistview_id (ptr_rule->type);

						if (rules_listview_id)
						{
							INT item_id = _r_listview_getitemcount (hwnd, rules_listview_id, FALSE);

							_r_fastlock_acquireshared (&lock_checkbox);

							_r_listview_additem (app.GetHWND (), rules_listview_id, item_id, 0, ptr_rule->pname, _app_getruleicon (ptr_rule), _app_getrulegroup (ptr_rule), rule_idx);
							_app_setruleiteminfo (app.GetHWND (), rules_listview_id, item_id, ptr_rule, TRUE);

							_r_fastlock_releaseshared (&lock_checkbox);

							if (rules_listview_id == listview_id)
								_app_listviewsort (app.GetHWND (), listview_id, INVALID_INT, FALSE);
						}

						// set app information
						PR_OBJECT ptr_app_object = _app_getappitem (app_hash);

						if (ptr_app_object)
						{
							PITEM_APP ptr_app = (PITEM_APP)ptr_app_object->pdata;

							if (ptr_app)
							{
								INT app_listview_id = _app_getlistview_id (ptr_app->type);

								if (app_listview_id)
								{
									INT item_pos = _app_getposition (app.GetHWND (), app_listview_id, app_hash);

									if (item_pos != INVALID_INT)
									{
										_r_fastlock_acquireshared (&lock_checkbox);
										_app_setappiteminfo (app.GetHWND (), app_listview_id, item_pos, app_hash, ptr_app);
										_r_fastlock_releaseshared (&lock_checkbox);
									}

									if (app_listview_id == listview_id)
										_app_listviewsort (app.GetHWND (), listview_id, INVALID_INT, FALSE);
								}
							}

							_r_obj2_dereference (ptr_app_object);
						}

						_app_refreshstatus (app.GetHWND (), listview_id);
						_app_profile_save (NULL);
					}

					_r_obj2_dereference (ptr_rule_object);

					break;
				}

				case IDM_COPY: // ctrl+c
				case IDM_SELECT_ALL: // ctrl+a
				{
					HWND hedit = GetFocus ();

					if (hedit)
					{
						WCHAR class_name[128];

						if (GetClassName (hedit, class_name, RTL_NUMBER_OF (class_name)) > 0)
						{
							if (_r_str_compare (class_name, WC_EDIT) == 0)
							{
								// edit control hotkey for "ctrl+c" (issue #597)
								if (ctrl_id == IDM_COPY)
									SendMessage (hedit, WM_COPY, 0, 0);

								// edit control hotkey for "ctrl+a"
								else if (ctrl_id == IDM_SELECT_ALL)
									SendMessage (hedit, EM_SETSEL, 0, (LPARAM)-1);
							}
						}
					}

					break;
				}
			}

			break;
		}
	}

	return FALSE;
}
