/*
 * Tool intended to help facilitate the process of booting Linux on Intel
 * Macintosh computers made by Apple from a USB stick or similar.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Copyright (C) 2013 SevenBits
 *
 */

#include <efi.h>
#include <efilib.h>

#include "menu.h"

#define KEYPRESS(keys, scan, uni) ((((UINT64)keys) << 32) | ((scan) << 16) | (uni))
#define EFI_SHIFT_STATE_VALID           0x80000000
#define EFI_RIGHT_CONTROL_PRESSED       0x00000004
#define EFI_LEFT_CONTROL_PRESSED        0x00000008
#define EFI_RIGHT_ALT_PRESSED           0x00000010
#define EFI_LEFT_ALT_PRESSED            0x00000020
#define EFI_CONTROL_PRESSED             (EFI_RIGHT_CONTROL_PRESSED|EFI_LEFT_CONTROL_PRESSED)
#define EFI_ALT_PRESSED                 (EFI_RIGHT_ALT_PRESSED|EFI_LEFT_ALT_PRESSED)
#define KEYPRESS(keys, scan, uni) ((((UINT64)keys) << 32) | ((scan) << 16) | (uni))
#define KEYCHAR(k) ((k) & 0xffff)
#define CHAR_CTRL(c) ((c) - 'a' + 1)

static EFI_STATUS key_read(UINT64 *key, BOOLEAN wait) {
	#define EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID \
		{ 0xdd9e7534, 0x7762, 0x4698, { 0x8c, 0x14, 0xf5, 0x85, 0x17, 0xa6, 0x25, 0xaa } }

	struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

	typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET_EX)(
		struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This;
		BOOLEAN ExtendedVerification;
	);

	typedef UINT8 EFI_KEY_TOGGLE_STATE;

	typedef struct {
		UINT32 KeyShiftState;
		EFI_KEY_TOGGLE_STATE KeyToggleState;
	} EFI_KEY_STATE;

	typedef struct {
		EFI_INPUT_KEY Key;
		EFI_KEY_STATE KeyState;
	} EFI_KEY_DATA;

	typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY_EX)(
		struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This;
		EFI_KEY_DATA *KeyData;
	);

	typedef EFI_STATUS (EFIAPI *EFI_SET_STATE)(
				struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This;
				EFI_KEY_TOGGLE_STATE *KeyToggleState;
	);

	typedef EFI_STATUS (EFIAPI *EFI_KEY_NOTIFY_FUNCTION)(
		EFI_KEY_DATA *KeyData;
	);

	typedef EFI_STATUS (EFIAPI *EFI_REGISTER_KEYSTROKE_NOTIFY)(
		struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This;
		EFI_KEY_DATA KeyData;
		EFI_KEY_NOTIFY_FUNCTION KeyNotificationFunction;
		VOID **NotifyHandle;
	);

	typedef EFI_STATUS (EFIAPI *EFI_UNREGISTER_KEYSTROKE_NOTIFY)(
		struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This;
		VOID *NotificationHandle;
	);

	typedef struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
		EFI_INPUT_RESET_EX Reset;
		EFI_INPUT_READ_KEY_EX ReadKeyStrokeEx;
		EFI_EVENT WaitForKeyEx;
		EFI_SET_STATE SetState;
		EFI_REGISTER_KEYSTROKE_NOTIFY RegisterKeyNotify;
		EFI_UNREGISTER_KEYSTROKE_NOTIFY UnregisterKeyNotify;
	} EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;

	EFI_GUID EfiSimpleTextInputExProtocolGuid = EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL_GUID;
	static EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *TextInputEx;
	static BOOLEAN checked;
	UINTN index;
	EFI_INPUT_KEY k;
	EFI_STATUS err;

	if (!checked) {
		err = LibLocateProtocol(&EfiSimpleTextInputExProtocolGuid, (VOID **)&TextInputEx);
		if (EFI_ERROR(err)) {
			TextInputEx = NULL;
		}

		checked = TRUE;
	}

	/* wait until key is pressed */
	if (wait) {
		if (TextInputEx)
			uefi_call_wrapper(BS->WaitForEvent, 3, 1, &TextInputEx->WaitForKeyEx, &index);
		else
			uefi_call_wrapper(BS->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
	}

	if (TextInputEx) {
		EFI_KEY_DATA keydata;
		UINT64 keypress;

		err = uefi_call_wrapper(TextInputEx->ReadKeyStrokeEx, 2, TextInputEx, &keydata);
		if (!EFI_ERROR(err)) {
			UINT32 shift = 0;

			/* do not distinguish between left and right keys */
			if (keydata.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) {
				if (keydata.KeyState.KeyShiftState & (EFI_RIGHT_CONTROL_PRESSED|EFI_LEFT_CONTROL_PRESSED)) {
					shift |= EFI_CONTROL_PRESSED;
				}
				if (keydata.KeyState.KeyShiftState & (EFI_RIGHT_ALT_PRESSED|EFI_LEFT_ALT_PRESSED)) {
					shift |= EFI_ALT_PRESSED;
				}
			};

			/* 32 bit modifier keys + 16 bit scan code + 16 bit unicode */
			keypress = KEYPRESS(shift, keydata.Key.ScanCode, keydata.Key.UnicodeChar);
			if (keypress > 0) {
				*key = keypress;
				return 0;
			}
		}
	}

	/* fallback for firmware which does not support SimpleTextInputExProtocol
	 *
	 * This is also called in case ReadKeyStrokeEx did not return a key, because
	 * some broken firmwares offer SimpleTextInputExProtocol, but never acually
	 * handle any key. */
	err  = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &k);
	if (EFI_ERROR(err)) {
		return err;
	}

	*key = KEYPRESS(0, k.ScanCode, k.UnicodeChar);
	return 0;
}

EFI_STATUS display_menu(void) {
	EFI_STATUS err;
	UINT64 key;
	
	/*
	 * Give the user some information as to what they can do at this point.
	 */
	display_colored_text(L"\n\n    Available boot options:\n");
	Print(L"    Press the key corresponding to the number of the option that you want.\n");
	Print(L"\n    1) Boot Linux from ISO file\n");
	Print(L"    2) Modify Linux kernel boot options (advanced!)\n");
	Print(L"\n    Press any other key to reboot the system.\n");
	
	err = key_read(&key, TRUE);
	if (key == '1') {
		Print(L"Boot Linux");
	} else if (key == '2') {
		Print(L"Configure Linux");
	} else {
		// Reboot the system.
		err = uefi_call_wrapper(RT->ResetSystem, 4, EfiResetCold, EFI_SUCCESS, 0, NULL);
		Print(L"Error calling ResetSystem: %r", err);
		uefi_call_wrapper(BS->Stall, 1, 3 * 1000 * 1000);
	}
	
	uefi_call_wrapper(BS->Stall, 1, 3 * 1000 * 1000);
	return EFI_SUCCESS;
}

VOID display_colored_text(CHAR16 *string) {
	uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_YELLOW|EFI_BACKGROUND_BLACK);
	Print(string);
	uefi_call_wrapper(ST->ConOut->SetAttribute, 2, ST->ConOut, EFI_LIGHTGRAY|EFI_BACKGROUND_BLACK);
}
