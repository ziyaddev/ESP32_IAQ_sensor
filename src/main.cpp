/*
 * I2C-Generator: 0.3.0
 * Yaml Version: 2.1.3
 * Template Version: 0.7.0-112-g190ecaa
 */
/*
 * Copyright (c) 2021, Sensirion AG
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of Sensirion AG nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#if defined(ESP32)
#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"
#elif defined(ESP8266)
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti wifiMulti;
#define DEVICE "ESP8266"
#endif

#include <Arduino.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <SensirionI2CSen5x.h>
#include <SensirionI2CSfa3x.h>
#include <Wire.h>
#include "creds.h"

// Time zone info
#define TZ_INFO "UTC1"

// Declare InfluxDB client instance with preconfigured InfluxCloud certificate
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Declare Data point
Point netwrk("wifi_status");
Point sen55_sensor("iaq_sensor");
Point sfa30_sensor("iaq_sensor");

// The used commands use up to 48 bytes. On some Arduino's the default buffer
// space is not large enough

#define MAXBUF_REQUIREMENT 48

#if (defined(I2C_BUFFER_LENGTH) &&                 \
	 (I2C_BUFFER_LENGTH >= MAXBUF_REQUIREMENT)) || \
	(defined(BUFFER_LENGTH) && BUFFER_LENGTH >= MAXBUF_REQUIREMENT)
#define USE_PRODUCT_INFO
#endif

SensirionI2CSen5x sen5x;
SensirionI2CSfa3x sfa3x;

void printModuleVersions()
{
	uint16_t sen55_error;
	char errorMessage[256];

	unsigned char productName[32];
	uint8_t productNameSize = 32;

	sen55_error = sen5x.getProductName(productName, productNameSize);

	if (sen55_error)
	{
		Serial.print("Error trying to execute getProductName() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}
	else
	{
		Serial.print("ProductName :");
		Serial.println((char *)productName);
	}

	uint8_t firmwareMajor;
	uint8_t firmwareMinor;
	bool firmwareDebug;
	uint8_t hardwareMajor;
	uint8_t hardwareMinor;
	uint8_t protocolMajor;
	uint8_t protocolMinor;

	sen55_error = sen5x.getVersion(firmwareMajor, firmwareMinor, firmwareDebug,
							 hardwareMajor, hardwareMinor, protocolMajor,
							 protocolMinor);
	if (sen55_error)
	{
		Serial.print("Error trying to execute getVersion() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}
	else
	{
		Serial.print("Firmware : ");
		Serial.print(firmwareMajor);
		Serial.print(".");
		Serial.print(firmwareMinor);
		Serial.print(", ");

		Serial.print("Hardware : ");
		Serial.print(hardwareMajor);
		Serial.print(".");
		Serial.println(hardwareMinor);
	}
}

void printSerialNumber()
{
	uint16_t sen55_error;
	char errorMessage[256];
	unsigned char serialNumber[32];
	uint8_t serialNumberSize = 32;

	sen55_error = sen5x.getSerialNumber(serialNumber, serialNumberSize);
	if (sen55_error)
	{
		Serial.print("Error trying to execute getSerialNumber() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}
	else
	{
		Serial.print("SerialNumber :");
		Serial.println((char *)serialNumber);
	}
}

void setup()
{
	Serial.begin(115200);
	while (!Serial)
	{
		delay(100);
	}

	// Setup wifi
	WiFi.mode(WIFI_STA);
	wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);

	Serial.print("Connecting to wifi");
	while (wifiMulti.run() != WL_CONNECTED)
	{
		Serial.print(".");
		delay(100);
	}
	Serial.println();

	// Accurate time is necessary for certificate validation and writing in batches
	// We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
	// Syncing progress and the time will be printed to Serial.
	timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

	// Check server connection
	if (client.validateConnection())
	{
		Serial.print("Connected to InfluxDB: ");
		Serial.println(client.getServerUrl());
	}
	else
	{
		Serial.print("InfluxDB connection failed: ");
		Serial.println(client.getLastErrorMessage());
	}

	Wire.begin();

	sen5x.begin(Wire);
    sfa3x.begin(Wire);

	uint16_t sen55_error;
    uint16_t sfa30_error;

	char errorMessage[256];
	sen55_error = sen5x.deviceReset();
	if (sen55_error)
	{
		Serial.print("Error trying to execute deviceReset() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}

// Print SEN55 module information if i2c buffers are large enough
#ifdef USE_PRODUCT_INFO
	printSerialNumber();
	printModuleVersions();
#endif

	// set a temperature offset in degrees celsius
	// Note: supported by SEN54 and SEN55 sensors
	// By default, the temperature and humidity outputs from the sensor
	// are compensated for the modules self-heating. If the module is
	// designed into a device, the temperature compensation might need
	// to be adapted to incorporate the change in thermal coupling and
	// self-heating of other device components.
	//
	// A guide to achieve optimal performance, including references
	// to mechanical design-in examples can be found in the app note
	// “SEN5x – Temperature Compensation Instruction” at www.sensirion.com.
	// Please refer to those application notes for further information
	// on the advanced compensation settings used
	// in `setTemperatureOffsetParameters`, `setWarmStartParameter` and
	// `setRhtAccelerationMode`.
	//
	// Adjust tempOffset to account for additional temperature offsets
	// exceeding the SEN module's self heating.
	float tempOffset = 0.0;
	sen55_error = sen5x.setTemperatureOffsetSimple(tempOffset);
	if (sen55_error)
	{
		Serial.print("Error trying to execute setTemperatureOffsetSimple() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}
	else
	{
		Serial.print("Temperature Offset set to ");
		Serial.print(tempOffset);
		Serial.println(" deg. Celsius (SEN54/SEN55 only");
	}

	// Start SEN55 Measurement
	sen55_error = sen5x.startMeasurement();
	if (sen55_error)
	{
		Serial.print("Error trying to execute startMeasurement() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}

    // Start SFA30 Measurement
    sfa30_error = sfa3x.startContinuousMeasurement();
    if (sfa30_error)
    {
        Serial.print("Error trying to execute startContinuousMeasurement(): ");
        errorToString(sfa30_error, errorMessage, 256);
        Serial.println(errorMessage);
    }

	// Add tags to the data point
	netwrk.addTag("device", DEVICE);
	netwrk.addTag("SSID", WiFi.SSID());
	sen55_sensor.addTag("Productname", "SEN55");
	sfa30_sensor.addTag("Productname", "SFA30");
}

void loop()
{
	uint16_t sen55_error;
	uint16_t sfa30_error;
	char errorMessage[256];

	delay(1000);

	// Read Measurements
	float massConcentrationPm1p0;
	float massConcentrationPm2p5;
	float massConcentrationPm4p0;
	float massConcentrationPm10p0;
	float ambientHumidity;
	float ambientTemperature;
	float vocIndex;
	float noxIndex;

    int16_t sfa30_hcho;
    int16_t sfa30_humidity;
    int16_t sfa30_temperature;

	sen55_error = sen5x.readMeasuredValues(
		massConcentrationPm1p0, massConcentrationPm2p5, massConcentrationPm4p0,
		massConcentrationPm10p0, ambientHumidity, ambientTemperature, vocIndex,
		noxIndex);

	if (sen55_error)
	{
		Serial.print("Error trying to execute readMeasuredValues() : ");
		errorToString(sen55_error, errorMessage, 256);
		Serial.println(errorMessage);
	}
	else
	{
		Serial.print("MassConcentrationPm1p0 :");
		Serial.print(massConcentrationPm1p0);
		Serial.print("\t");
		Serial.print("MassConcentrationPm2p5 :");
		Serial.print(massConcentrationPm2p5);
		Serial.print("\t");
		Serial.print("MassConcentrationPm4p0 :");
		Serial.print(massConcentrationPm4p0);
		Serial.print("\t");
		Serial.print("MassConcentrationPm10p0 :");
		Serial.print(massConcentrationPm10p0);
		Serial.print("\t");
		Serial.print("AmbientHumidity :");
		if (isnan(ambientHumidity))
		{
			Serial.print("n/a");
		}
		else
		{
			Serial.print(ambientHumidity);
		}
		Serial.print("\t");
		Serial.print("AmbientTemperature :");
		if (isnan(ambientTemperature))
		{
			Serial.print("n/a");
		}
		else
		{
			Serial.print(ambientTemperature);
		}
		Serial.print("\t");
		Serial.print("VocIndex :");
		if (isnan(vocIndex))
		{
			Serial.print("n/a");
		}
		else
		{
			Serial.print(vocIndex);
		}
		Serial.print("\t");
		Serial.print("NoxIndex :");
		if (isnan(noxIndex))
		{
			Serial.println("n/a");
		}
		else
		{
			Serial.println(noxIndex);
		}
	}

    sfa30_error = sfa3x.readMeasuredValues(sfa30_hcho, sfa30_humidity, sfa30_temperature);
    if (sfa30_error)
    {
        Serial.print("Error trying to execute readMeasuredValues(): ");
        errorToString(sfa30_error, errorMessage, 256);
        Serial.println(errorMessage);
    }
    else
    {
        Serial.print("Hcho:");
        Serial.print(sfa30_hcho / 5.0);
        Serial.print("\t");
        Serial.print("Humidity:");
        Serial.print(sfa30_humidity / 100.0);
        Serial.print("\t");
        Serial.print("Temperature:");
        Serial.println(sfa30_temperature / 200.0);
    }

	// Clear fields for reusing the point. Tags will remain the same as set above.
	netwrk.clearFields();
	sen55_sensor.clearFields();
	sfa30_sensor.clearFields();

	// Store measured value into point
	// Report RSSI of currently connected network
	netwrk.addField("rssi", WiFi.RSSI());
	sen55_sensor.addField("PM1.0", massConcentrationPm1p0);
	sen55_sensor.addField("PM2.5", massConcentrationPm2p5);
	sen55_sensor.addField("PM4.0", massConcentrationPm4p0);
	sen55_sensor.addField("PM10.0", massConcentrationPm10p0);
	sen55_sensor.addField("Temperature", ambientTemperature);
	sen55_sensor.addField("Humidity", ambientHumidity);
	sen55_sensor.addField("VOC Index", vocIndex);
	sen55_sensor.addField("NOX Index", noxIndex);
	sfa30_sensor.addField("H2CO Concentration", sfa30_hcho);
	sfa30_sensor.addField("SFA30 Temperature", sfa30_temperature);
	sfa30_sensor.addField("SFA30 Humidity", sfa30_humidity);

	// Print what are we exactly writing
	Serial.print("Writing : ");
	Serial.println(netwrk.toLineProtocol());
	Serial.println(sen55_sensor.toLineProtocol());
	Serial.println(sfa30_sensor.toLineProtocol());

	// Check WiFi connection and reconnect if needed
	if (wifiMulti.run() != WL_CONNECTED)
	{
		Serial.println("Wifi connection lost");
	}

	// Write points
	if (!client.writePoint(netwrk)) {
		Serial.print("InfluxDB write failed: ");
		Serial.println(client.getLastErrorMessage());
	}
	if (!client.writePoint(sen55_sensor)) {
		Serial.print("InfluxDB write failed: ");
		Serial.println(client.getLastErrorMessage());
	}
	if (!client.writePoint(sfa30_sensor)) {
		Serial.print("InfluxDB write failed: ");
		Serial.println(client.getLastErrorMessage());
	}
}
