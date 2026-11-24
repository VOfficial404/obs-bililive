/*
obs-bililive
Copyright (C) 2026 VOfficial404

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "secret-store.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#endif

namespace bililive {

namespace {

#if defined(_WIN32)

const QByteArray kPrefix = "dpapi:";
// Additional entropy: other programs running as the same user can't decrypt it without knowing this too.
const QByteArray kEntropy = "obs-bililive/session";

DATA_BLOB blobOf(const QByteArray &data)
{
	return {static_cast<DWORD>(data.size()), reinterpret_cast<BYTE *>(const_cast<char *>(data.constData()))};
}

QByteArray seal(const QByteArray &secret)
{
	DATA_BLOB in = blobOf(secret), entropy = blobOf(kEntropy), out{};
	if (!CryptProtectData(&in, L"obs-bililive", &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};
	const QByteArray sealed =
		kPrefix +
		QByteArray(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData)).toBase64();
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return sealed;
}

QByteArray open(const QByteArray &sealed)
{
	if (!sealed.startsWith(kPrefix))
		return {};
	const QByteArray encrypted = QByteArray::fromBase64(sealed.mid(kPrefix.size()));
	DATA_BLOB in = blobOf(encrypted), entropy = blobOf(kEntropy), out{};
	if (!CryptUnprotectData(&in, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};
	QByteArray secret(reinterpret_cast<const char *>(out.pbData), static_cast<qsizetype>(out.cbData));
	SecureZeroMemory(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return secret;
}

void erase() {}

#elif defined(__APPLE__)

const QByteArray kMarker = "keychain";

CFMutableDictionaryRef itemQuery()
{
	CFMutableDictionaryRef query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
								 &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
	CFDictionarySetValue(query, kSecAttrService, CFSTR("obs-bililive"));
	CFDictionarySetValue(query, kSecAttrAccount, CFSTR("session"));
	return query;
}

void erase()
{
	CFMutableDictionaryRef query = itemQuery();
	SecItemDelete(query);
	CFRelease(query);
}

QByteArray seal(const QByteArray &secret)
{
	erase();
	CFMutableDictionaryRef item = itemQuery();
	CFDataRef value = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(secret.constData()),
				       static_cast<CFIndex>(secret.size()));
	CFDictionarySetValue(item, kSecValueData, value);
	CFDictionarySetValue(item, kSecAttrAccessible, kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly);
	const OSStatus status = SecItemAdd(item, nullptr);
	CFRelease(value);
	CFRelease(item);
	return status == errSecSuccess ? kMarker : QByteArray();
}

QByteArray open(const QByteArray &sealed)
{
	if (sealed != kMarker)
		return {};
	CFMutableDictionaryRef query = itemQuery();
	CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
	CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
	CFTypeRef result = nullptr;
	const OSStatus status = SecItemCopyMatching(query, &result);
	CFRelease(query);
	if (status != errSecSuccess || !result)
		return {};
	const auto data = static_cast<CFDataRef>(result);
	QByteArray secret(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
			  static_cast<qsizetype>(CFDataGetLength(data)));
	CFRelease(result);
	return secret;
}

#else

const QByteArray kPrefix = "plain:";

QByteArray seal(const QByteArray &secret)
{
	return kPrefix + secret.toBase64();
}

QByteArray open(const QByteArray &sealed)
{
	return sealed.startsWith(kPrefix) ? QByteArray::fromBase64(sealed.mid(kPrefix.size())) : QByteArray();
}

void erase() {}

#endif

} // namespace

QByteArray sealSecret(const QByteArray &secret)
{
	if (secret.isEmpty()) {
		erase();
		return {};
	}
	return seal(secret);
}

QByteArray openSecret(const QByteArray &sealed)
{
	return sealed.isEmpty() ? QByteArray() : open(sealed);
}

} // namespace bililive
