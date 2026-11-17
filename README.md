# RF Grow Light Controller

An ESP8266 and CC1101 controller that replays a grow light's 433.92 MHz remote commands. It provides a daily lighting schedule and a small HTTP API for switching the light, adjusting brightness, selecting the light's built-in timer, and changing radio power.

No companion device or app is required. Configure the firmware in the Arduino IDE, then use `curl` or any HTTP client to control it. This README describes the supplied `Light-Remote-Control.ino`, including its current limitations.

## Hardware

- ESP8266 development board with the D1/D5/D6/D7/D8 pin mapping used below.
- CC1101 module suitable for 433 MHz operation and a suitable antenna.
- Compatible grow light and its original remote for identifying command values.
- Appropriate power supply and wiring.

The command values in this project were captured from one particular remote. They are not universal grow light codes. Use commands and radio parameters verified for your own equipment.

### Wiring

| CC1101 connection | ESP8266 connection | Purpose |
| --- | --- | --- |
| VCC | 3.3 V | Module power |
| GND | GND | Common ground |
| SCK | D5 / GPIO14 | SPI clock |
| MISO | D6 / GPIO12 | SPI data to ESP8266 |
| MOSI | D7 / GPIO13 | SPI data to CC1101 |
| CS / CSN | D8 / GPIO15 | SPI chip select |
| GDO0 | D1 / GPIO5 | Transmit pulse output from ESP8266 |

Check the labels and power requirements of your particular module before connecting it. The controller sketch does not use a GDO2 receive input or decode remote button presses.

## Build and configure

1. Install ESP8266 board support in the Arduino IDE and select the board matching your hardware.
2. Install the libraries providing these headers:

   | Header | Dependency |
   | --- | --- |
   | `ESP8266WiFi.h`, `ESP8266WebServer.h` | ESP8266 Arduino core |
   | `ELECHOUSE_CC1101_SRC_DRV.h` | ELECHOUSE CC1101 driver |
   | `RCSwitch.h` | rc-switch |
   | `ArduinoJson.h` | ArduinoJson with the `JsonDocument` API used by the sketch, such as version 7 |
   | `time.h` | Toolchain/core time support |

3. Open `Light-Remote-Control.ino`. If the Arduino IDE requests a matching sketch folder, allow it to create one.
4. Edit the network, credentials, radio codes, and schedule settings described below.
5. Upload the sketch and open Serial Monitor at **115200 baud**.

Dependency versions and a specific ESP8266 board profile are not pinned in the supplied source.

### Network and API credentials

Replace these source constants before uploading:

```cpp
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

IPAddress sensorIP(192, 168, 40, 99);
IPAddress gateway(192, 168, 40, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 40, 1);

const char* www_username = "YOUR_CONTROLLER_USERNAME";
const char* www_password = "YOUR_CONTROLLER_PASSWORD";
```

Choose an unused static address on your LAN and set a reachable gateway and DNS server. The supplied source's DNS address is specific to its original network; replace it. Wi-Fi credentials, IP configuration, and API credentials cannot be changed through the HTTP API.

The server listens on **HTTP port 80**. State-changing endpoints use HTTP Basic authentication. HTTP does not encrypt the credentials or commands, so use a trusted or isolated LAN and do not expose the controller directly to the internet. `/info` and `/brew` are unauthenticated in this firmware.

### Radio settings and command primitives

The supplied configuration is:

| Setting | Value |
| --- | --- |
| Frequency | 433.92 MHz |
| RCSwitch protocol | 1 |
| Message length | 24 bits |
| Pulse length | 350 microseconds |
| Repetitions per command | 4 |
| Initial radio power setting | −15 dBm |

The firmware defines these command primitives:

| Constant | Value | Action | HTTP access |
| --- | --- | --- | --- |
| `CODE_ON` | `0x555501` | Power on | `/light` |
| `CODE_OFF` | `0x555503` | Power off | `/light` |
| `CODE_LIGHT_INC` | `0x555504` | Brightness up | Used by `/intensity` |
| `CODE_LIGHT_MIN` | `0x555506` | Brightness down | Used by `/intensity` |
| `CODE_RST` | `0x555507` | Remote reset command | No endpoint |
| `CODE_MODE` | `0x555509` | Light mode | No endpoint |
| `CODE_SET_4H` | `0x55550D` | Built-in 4-hour timer | `/setdelay` |
| `CODE_SET_8H` | `0x55550F` | Built-in 8-hour timer | `/setdelay` |
| `CODE_SET_12H` | `0x555510` | Built-in 12-hour timer | `/setdelay` |
| `CODE_CLEAR` | `0x555512` | Remote clear command | No endpoint |

To use an existing primitive directly in your own firmware logic, call it **after radio initialization**:

```cpp
sendRFCommand(CODE_ON);
// Other examples, to use individually when needed:
// sendRFCommand(CODE_OFF);
// sendRFCommand(CODE_MODE);
// sendRFCommand(CODE_CLEAR);
```

Direct calls only transmit the command; they do not automatically update `lightState`, `delayHours`, or other tracking variables. These examples are alternatives, not a sequence to paste into the continuously running loop. There is no HTTP endpoint for arbitrary raw codes.

### Schedule and time

The compile-time defaults are:

```cpp
int TURN_ON_HOUR = 8;
int TURN_OFF_HOUR = 20;
```

This requests lighting from **08:00 inclusive to 20:00 exclusive**, using the controller's local time. The schedule is checked in the main loop, which includes a one-second delay. HTTP requests and radio commands can extend that interval.

The sketch obtains time from `time.nist.gov`. It uses a fixed **UTC−7** offset in **both `setup()` and `handleGetInfo()`**:

```cpp
configTime(-7 * 3600, 0, "time.nist.gov");
```

Change both calls if a different offset is required. This configuration does not automatically adjust for daylight saving time. Schedule processing waits until the sketch's basic time-validity check passes (`tm_year > 70`); that check does not prove the clock is accurate.

Use same-day schedules where the on hour is less than the off hour. Overnight windows are not implemented. The current HTTP validator accepts integers from **1 through 24**, although the clock reports hours from **0 through 23**. For predictable API use, choose **1–23**; midnight cannot currently be configured correctly through the endpoint.

## Control with curl

Set your controller address and the username you compiled into the sketch:

```sh
CONTROLLER_URL='http://192.168.1.99'
CONTROLLER_USER='YOUR_CONTROLLER_USERNAME'
```

The authenticated examples use `--user "$CONTROLLER_USER"`, which prompts for the password. Send JSON with the exact shapes shown below. `/light` uses text matching rather than a JSON parser, so retain the shown formatting and use JSON booleans rather than quoted strings.

### Read status

```sh
curl --silent --show-error "$CONTROLLER_URL/info"
```

Example response shape; values vary:

```json
{
  "result": "success",
  "currentTime": "2026-11-16 12:00:00",
  "internalDelay": 0,
  "turnOnHour": 8,
  "turnOffHour": 20,
  "hourlyOnCounter": 215900,
  "radioPowerLevel": -15,
  "lightIntensity": 10,
  "lightState": 1
}
```

| Field | Meaning |
| --- | --- |
| `currentTime` | Controller time formatted using its configured offset |
| `internalDelay` | Last built-in light timer requested: initially 0, then 4, 8, or 12; not a remaining-time countdown |
| `turnOnHour`, `turnOffHour` | Current in-memory schedule |
| `hourlyOnCounter` | Loop countdown for reasserting ON; not elapsed hours or a reliable seconds value |
| `radioPowerLevel` | Tracked power setting; see timer bookkeeping issue below |
| `lightIntensity` | Last requested brightness value; initially 10 |
| `lightState` | Tracked light state as numeric 0 or 1, not receiver feedback |

The serial connection message advertises `/sensor`, but that route does not exist. Use `/info`.

### Turn the light on

```sh
curl --silent --show-error --user "$CONTROLLER_USER" \
  -H 'Content-Type: application/json' \
  --data '{"state":true}' \
  "$CONTROLLER_URL/light"
```

Expected success body:

```json
{"accepted":true,"light":true}
```

### Turn the light off

```sh
curl --silent --show-error --user "$CONTROLLER_USER" \
  -H 'Content-Type: application/json' \
  --data '{"state":false}' \
  "$CONTROLLER_URL/light"
```

Expected success body:

```json
{"accepted":true,"light":false}
```

**Manual control has an asymmetric override in the current firmware.** Either command sets `lightStaysOn = true`, which suppresses scheduled OFF and periodic ON reassertion. However, the scheduled ON transition does not check that flag: a manual OFF during scheduled lighting hours can be reversed on the next loop. A manual ON can remain on beyond the scheduled off hour.

There is no API endpoint to clear this override. Reboot to restore the compile-time defaults and normal scheduling, or change the firmware to implement an explicit automatic/manual mode. Updating `/setschedule` does not clear it.

### Set brightness

```sh
curl --silent --show-error --user "$CONTROLLER_USER" \
  -H 'Content-Type: application/json' \
  --data '{"value":5}' \
  "$CONTROLLER_URL/intensity"
```

Use integer values **1–10**, the range advertised by the handler. The actual physical brightness mapping needs calibration: the routine sends ten brightness-down commands, followed by `value + 1` brightness-up commands because its loop uses `<=`. It does not measure brightness or confirm the number of physical levels.

This operation blocks the request while transmitting. Its explicit delays alone take approximately **6–10.5 seconds** across values 1–10, plus RF transmission time. Allow a generous client timeout, for example `--max-time 60`.

### Set transmit power

```sh
curl --silent --show-error --user "$CONTROLLER_USER" \
  -H 'Content-Type: application/json' \
  --data '{"value":-15}' \
  "$CONTROLLER_URL/powerlevel"
```

Use one of the settings the controller intends to expose: **−30, −20, −15, −10, or 0 dBm**. Start with the lowest setting that works reliably for your equipment. This changes the CC1101 transmitter setting, not the grow light's brightness or electrical power consumption.

### Select the light's built-in timer

```sh
curl --silent --show-error --user "$CONTROLLER_USER" \
  -H 'Content-Type: application/json' \
  --data '{"value":8}' \
  "$CONTROLLER_URL/setdelay"
```

Accepted values are **4, 8, or 12**. This sends the corresponding remote timer command. It does not change the ESP8266's daily schedule or pause it. The light's internal timer and the controller's schedule can therefore issue conflicting behavior. Use one timing approach at a time until you have verified your light's behavior.

`CODE_CLEAR` is defined in the source but has no HTTP endpoint. The firmware does not establish exactly how the light's built-in timer behaves across power cycles.

### Change the daily schedule

```sh
curl --silent --show-error --user "$CONTROLLER_USER" \
  -H 'Content-Type: application/json' \
  --data '{"onhour":8,"offhour":20}' \
  "$CONTROLLER_URL/setschedule"
```

Expected success body:

```json
{"result":"success","value":{"start":8,"end":20}}
```

The correct path is **`/setschedule`**, despite the `/schedule` example comment in the source. Both fields are required for a useful update. The new schedule is evaluated by the main loop, subject to the manual-override behavior above.

### Teapot check

```sh
curl -i "$CONTROLLER_URL/brew"
```

Returns HTTP **418** and an `I'm a teapot` message. It does not control the light.

## HTTP API reference

All request bodies below are JSON. Auth means the configured HTTP Basic credentials.

| Method | Path | Auth | Request body | Purpose |
| --- | --- | --- | --- | --- |
| GET | `/info` | No | None | Read controller status |
| POST | `/light` | Yes | `{"state":true}` or `{"state":false}` | Send ON/OFF |
| POST | `/intensity` | Yes | `{"value":5}` | Request brightness |
| POST | `/powerlevel` | Yes | `{"value":-15}` | Set transmitter power |
| POST | `/setdelay` | Yes | `{"value":8}` | Send built-in timer command |
| POST | `/setschedule` | Yes | `{"onhour":8,"offhour":20}` | Change daily hours in RAM |
| GET | `/brew` | No | None | Return HTTP 418 |

Unknown routes return HTTP 404. Protected routes request authentication when credentials are missing or incorrect. `/light` returns HTTP 400 for an unrecognized state body. Several other handlers return HTTP 200 even when reporting a failure, and some error bodies are malformed JSON; do not use HTTP status alone as proof that a configuration was accepted.

## Persistence and state

Runtime changes are stored in RAM. Rebooting reloads the compiled schedule, radio power, brightness tracking value, and manual-control flag. There is no EEPROM or filesystem configuration save endpoint.

`lightState`, `lightIntensity`, and `internalDelay` describe the controller's assumptions or last requests. The receiver does not acknowledge commands to this sketch. Using the original remote, losing an RF transmission, or allowing the light's internal timer to expire can leave the physical light out of sync with `/info`.

Booting with `lightState = false` does not send an OFF command. If the clock becomes valid during scheduled hours, the controller sends ON; outside those hours, a light already on may remain on because the controller initially assumes it is off.

## Current implementation notes

These are source-review findings, not features promised by the API:

- **Range checks:** `/intensity` and `/powerlevel` use `||` where a bounded range check would require `&&`. They do not reliably reject out-of-range numeric inputs. The power handler also does not enforce a discrete allowed-value list. Send only the documented values.
- **JSON validation:** JSON-based handlers ignore `deserializeJson()` errors and do not consistently validate field presence or types. Use complete requests with numeric values as shown.
- **Timer bookkeeping:** `/setdelay` accidentally assigns its timer value to `powerLevel` without applying it to the radio. After setting a timer, `/info` can show 4, 8, or 12 as the radio power even though that endpoint did not change the hardware power. Reapplying `/powerlevel` restores the tracking value.
- **Reassertion interval:** `hourlyCheck` starts at 216000 and decreases once per loop. With the one-second delay, that is roughly 60 hours or longer, not one hour. It is not a clock-based timer.
- **Wi-Fi retry timing:** the retry condition subtracts a variable named `lastWiFiRetryMillis` from `time(nullptr)`, which returns seconds, then compares against 5000. This mixes intended milliseconds with epoch seconds; the interval is roughly 5000 seconds when the clock is progressing normally, rather than the intended five seconds.
- **Blocking work:** connection attempts can block for about 15 seconds, and brightness changes block while sending multiple commands. HTTP service and scheduling can pause during these operations.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Cannot connect | Verify the compiled static IP, subnet, Wi-Fi credentials, power, and Serial Monitor output. Use HTTP on port 80. |
| `/sensor` or `/schedule` returns 404 | Use `/info` or `/setschedule`, respectively. |
| HTTP 401 | Check `www_username` and `www_password` from the flashed sketch. |
| Light does not respond despite success | Check wiring, antenna, power setting, range, and whether the captured codes and timing match your light. API success is not an RF acknowledgment. |
| Schedule runs at the wrong time | Inspect `/info`; check DNS/NTP reachability and both fixed-offset `configTime()` calls. |
| Manual OFF immediately reverses | Scheduled ON currently ignores the manual override flag. |
| Light stays on after its off hour | A previous `/light` request may have enabled the override; reboot restores default scheduling. |
| Settings disappear | Runtime settings are not persisted. Edit source defaults and reflash for durable configuration. |
| Brightness request seems slow | Allow time for the blocking RF sequence; use a generous timeout. |

## Project background

This grew out of capturing my own grow light remote and replaying its commands to give my plants a winter lighting schedule. The HTTP API makes those same controls available to shell scripts, home automation clients, or another device without requiring a particular companion interface.
