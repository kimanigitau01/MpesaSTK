/*
   MIT License

  Copyright (c) 2021 Felix Biego

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecureBearSSL.h>

HTTPClient https;

const char *ssid = "TUK-WIFI";
const char *password = "P@ssword@123";

// extern "C" {
// #include "crypto/base64.h"
// }

#include "MpesaSTK.h"

String encoder64(const String &encodedStr)
{
	const String base64Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	String decodedStr;
	int val = 0, valb = -8;
	for (char c : encodedStr)
	{
		if (c == '=')
			break;
		if (c >= 'A' && c <= 'Z')
			c -= 'A';
		else if (c >= 'a' && c <= 'z')
			c = c - 'a' + 26;
		else if (c >= '0' && c <= '9')
			c = c - '0' + 52;
		else if (c == '+')
			c = 62;
		else if (c == '/')
			c = 63;
		else
			continue;

		val = (val << 6) | c;
		valb += 6;

		if (valb >= 0)
		{
			decodedStr += (char((val >> valb) & 0xFF));
			valb -= 8;
		}
	}
	return decodedStr;
}

/*!
	@brief  Constructor for MpesaSTK
	@param  consumer_key
			Consumer Key: https://developer.safaricom.co.ke/user/me/apps
	@param  consumer_secret
			Consumer Secret: https://developer.safaricom.co.ke/user/me/apps
	@param  pass_key
			Lipa Na Mpesa Online PassKey: https://developer.safaricom.co.ke/test_credentials
	@param  environment
			Environment: SANDBOX(default), PRODUCTION
*/
MpesaSTK::MpesaSTK(String consumer_key, String consumer_secret, String pass_key, String environment)
{
	_consumer_key = consumer_key;
	_consumer_secret = consumer_secret;
	_pass_key = pass_key;
	_environment = environment;
}

/*!
	@brief  begin
	@param  code
			Lipa Na Mpesa Online Shortcode
	@param  type
			Transaction Type: PAYBILL(default), BUY_GOODS
	@param  url
			callback url
	@param  gmt
			GMT offset
*/
void MpesaSTK::begin(int code, String type, String url, int gmt, bool set)
{

	_business_code = code;
	_transaction_type = type;
	_callback_url = url;

	if (set)
	{
		setTime(gmt);
	}
}

/*!
	@brief  initiate the payment process
	@param  number
			12 digit phone number (2457XXXXXXXX)
	@param  amount
			Amount to be paid
	@param  reference
			Transaction reference
	@param  description
			Transaction description
*/
String MpesaSTK::pay(String number, int amount, String reference, String description)
{
	_number = number;
	_amount = amount;
	_reference = reference;
	_description = description;

	if (getToken())
	{
		return stkPush();
	}
	else
	{
		return "{\"Result\":\"failed\",\"Message\":\"Failed to get an access token\"}";
	}
}

/*!
	@brief  get the access token
*/
bool MpesaSTK::getToken()
{
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	Serial.print("Connecting to WiFi ..");
	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.print('.');
		delay(1000);
	}
	bool success = false;
	if ((WiFi.status() == WL_CONNECTED))
	{
		std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
		client->setInsecure();

		int httpCodeStatus = https.GET();
		Serial.printf("[HTTPS] GET... code: %d\n", httpCodeStatus);

		String encoded = encoder64(_consumer_key + ":" + _consumer_secret);
		https.begin(*client, _environment + "oauth/v1/generate?grant_type=client_credentials"); // https://sandbox.safaricom.co.ke/oauth/v1/generate?grant_type=client_credentials
		https.addHeader("authorization", "Basic " + encoded);
		https.addHeader("cache-control", "no-cache");
		int httpCode = https.GET();
		success = false;
		if (httpCode > 0)
		{
			Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
			if (httpCode == HTTP_CODE_OK)
			{
				String payload = https.getString();
				payload.replace("\n", "");
				payload.replace(" ", "");
				String st = "{\"access_token\":\"";
				int start = payload.indexOf(st);
				int end = payload.indexOf("\",\"expires_in\":");
				if (start != -1 && end != -1)
				{
					_token = payload.substring(st.length(), end);
					success = true;
				}
			}
		}
		https.end();
	}
	return success;
}

/*!
	@brief  encodes the input string in base64 format
	@param  input
			String to be encoded
*/
// String MpesaSTK::encoder64(String input)
// {
// 	char toEncode[input.length() + 1];
// 	input.toCharArray(toEncode, input.length() + 1);
// 	size_t outputLength;
// 	unsigned char *encoded = base64_encode((const unsigned char *)toEncode, strlen(toEncode), &outputLength);
// 	String encodedString = (const char *)encoded;
// 	free(encoded);
// 	encodedString.replace("\n", "");
// 	return encodedString;
// }

/*!
	@brief  set the time
*/
void MpesaSTK::setTime(int gmt, int dst)
{
	configTime(gmt * 3600, dst * 3600, "pool.ntp.org", "time.nist.gov");
	time_t nowSecs = time(nullptr);
	while (nowSecs < 1619827200)
	{
		delay(500);
		yield();
		nowSecs = time(nullptr);
	}
	struct tm timeinfo;
	gmtime_r(&nowSecs, &timeinfo);
}

/*!
	@brief  get the internal RTC time as a tm struct
*/
tm MpesaSTK::getTimeStruct()
{
	struct tm timeinfo;
	getLocalTime(&timeinfo);
	return timeinfo;
}

/*!
	@brief  get the time as an Arduino String object with the specified format
	@param	format
			time format
			http://www.cplusplus.com/reference/ctime/strftime/
*/
String MpesaSTK::getTime(String format)
{
	struct tm timeinfo = getTimeStruct();
	char s[128];
	char c[128];
	format.toCharArray(c, 127);
	strftime(s, 127, c, &timeinfo);
	return String(s);
}

/*!
	@brief  perform the STK request
*/
String MpesaSTK::stkPush()
{
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	Serial.print("Connecting to WiFi ..");
	String result = "";
	while (WiFi.status() != WL_CONNECTED)
	{
		Serial.print('.');
		delay(1000);
	}
	if ((WiFi.status() == WL_CONNECTED))
	{
		std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
		client->setInsecure();

		int httpCodeStatus = https.GET();
		Serial.printf("[HTTPS] GET... code: %d\n", httpCodeStatus);

		String timestamp = getTime("%Y%m%d%H%M%S");
		String password = encoder64(_business_code + _pass_key + timestamp);
		String json = "{\"BusinessShortCode\":\"" + String(_business_code) + "\",\"Password\":\"" + password + "\",\"Timestamp\":\"" + timestamp + "\",\"TransactionType\":\"" + _transaction_type + "\",\"Amount\":\"" + String(_amount) + "\",\"PartyA\":\"" + _number + "\",\"PartyB\":\"" + String(_business_code) + "\",\"PhoneNumber\":\"" + _number + "\",\"CallBackURL\":\"" + _callback_url + "\",\"AccountReference\":\"" + _reference + "\",\"TransactionDesc\":\"" + _description + "\"}";
		https.begin(*client, _environment + "mpesa/stkpush/v1/processrequest");
		https.addHeader("Content-Type", "application/json");
		https.addHeader("authorization", "Bearer " + _token);
		https.addHeader("cache-control", "no-cache");
		int httpCode = https.POST(json);
		result = "";
		if (httpCode > 0)
		{
			Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
			if (httpCode == HTTP_CODE_OK)
			{
				String payload = https.getString();
				payload.replace("\n", "");
				payload.replace(" ", "");
				payload.replace("}", ",\"Result\":\"success\",\"HttpCode\":\"" + String(httpCode) + "\"}");
				result = payload;
			}
			else
			{
				result = "{\"Result\":\"failed\",\"HttpCode\":\"" + String(httpCode) + "\"}";
			}
		}
		else
		{
			result = "{\"Result\":\"failed\",\"HttpCode\":\"" + String(httpCode) + "\"}";
		}

		https.end();
	}
	return result;
}