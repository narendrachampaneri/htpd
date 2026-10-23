---
title: Field Test Report 2026
---

# Summary

Between March and August 2026, forty chargers of the new series were operated at twelve sites in three countries. The chargers delivered 61,200 charging sessions with a success rate of 97.4 percent. The most frequent cause of a failed session was a communication timeout with vehicles of one manufacturer, which was fixed by a software update in June.

# Test setup

The sites were chosen to cover hot and cold climates, motorway service areas and city car parks. Each charger recorded every session with its start and end time, the energy delivered, the peak power and the reason why the session ended.

The data was collected every night and checked for gaps. Three chargers lost their network connection for more than a week; their data was read from the local memory afterwards, so no session was lost.

# Results

## Availability

The chargers were available for 99.1 percent of the time. Planned maintenance accounts for 0.4 percent and faults for 0.5 percent. The longest outage lasted 31 hours and was caused by a damaged supply cable at a motorway site.

## Charging sessions

The average session delivered 28.4 kilowatt hours in 24 minutes. Sessions at motorway sites were shorter and delivered more power than sessions in city car parks, where vehicles often stayed connected after charging had finished.

| Site type | Sessions | Success rate | Average energy |
|---|---|---|---|
| Motorway | 38,900 | 97.9 % | 31.2 kWh |
| City | 17,400 | 96.6 % | 23.1 kWh |
| Depot | 4,900 | 97.0 % | 26.5 kWh |

## Failures

Of the 1,590 failed sessions, 910 ended with a communication timeout, 380 with an insulation fault reported by the vehicle, 190 with a user stop before energy was delivered, and 110 with other causes. After the software update in June the share of communication timeouts fell from 1.9 to 0.6 percent of all sessions.

# Conclusions

The new series is ready for volume production. Two changes are recommended before the launch: the supply cable should be routed through a protective conduit at motorway sites, and the timeout for the first message of the vehicle should be raised from 2 to 5 seconds.

# Appendix: Sites

The twelve sites were located in Germany, Sweden and Spain. Four sites were motorway service areas, six were city car parks and two were bus depots.
