// Using Choosen from a phone on the same network. Off by default. When on, a second listener
// (port + 1, every network interface) serves the same page to paired phones only: a phone pairs
// once by scanning the QR code shown on the PC (or typing its 6-digit code) and then carries a
// random token in a cookie.
// Phones can swipe, browse and rate; they cannot change settings, pick folders or quit.
#pragma once
#include "http.h"

#include <string>
#include <vector>

void remoteInit(HttpHandler handler);  // loads paired phones; starts the listener if the setting is on
void remoteApply();                    // after a settings change: starts or stops the listener
bool remoteListening();
int remotePort();
std::vector<std::string> remoteAddresses();  // "http://<address>:<port>/" per network address

std::string remoteStartPairing();  // a new code, valid for 5 minutes or 5 wrong tries
// Checks a code; on success returns a new token for the phone (stored in data\remote-devices.txt).
// A phone that pairs again passes the token it already has, which the new one replaces.
bool remotePair(const std::string& code, std::string& token, const std::string& previous = "");
std::string remoteToken(const HttpRequest& q);  // the token in a request's cookie, "" if none
bool remoteAuthorized(const HttpRequest& q);  // carries the token of a paired phone
int remoteDeviceCount();
void remoteForgetAll();
// For the settings tab. v3: the QR link opens the V3 page (/v3/) instead of the normal one.
std::string remoteStateJson(bool v3 = false);

// "" if a request to the phone listener may be answered at all (address, origin, headers), else
// the reason. Pairing and the token are checked separately.
std::string remoteCheckRequest(const HttpRequest& q, int port);
// The page's own files, which an unpaired phone may load to show the pairing screen: "/", the
// normal page's files ("/app.js") and exactly the V3 bundle ("/v3", "/v3/", "/v3/app.js"). No photos.
bool remotePageFile(const std::string& path);
// Paths a phone may use once paired (settings, folder picking and quitting stay on the PC).
bool remoteAllowedPath(const std::string& method, const std::string& path);
