# nRF Fuel Gauge battery models

## generic_80mAh.inc (RevF tracker)

Provisional model for the protected 401220 80 mAh / 4.2 V cell. It is **not**
a measured profile of that cell, so the reported percentage is an estimate.

- Source: Nordic PowerUP "Generic 100mAh - 4.2V" model for nPM1304,
  [pc-nrfconnect-npm@17e4f51](https://github.com/nordicsemi/pc-nrfconnect-npm/blob/17e4f51fb120751247b491356b30a70bbf9d7bdd/resources/batteryModels/npm1304/Generic/100mAh%20-%204.2V.inc),
  original SHA256 `08804339b39105e5110ed3ef01b96c85cde07a7be9419331f7a23597ebc5c603`.
- Adaptation: capacity `param_12` changed from 0.100 Ah to 0.080 Ah and the name
  changed; all other parameters unchanged. Capacity interpretation was checked
  against Nordic's published 50 and 100 mAh generic models.
- The field layout matches nRF Fuel Gauge in NCS 3.4.

Replace the whole file with a profiled 401220 model when one is available.
Charger limits are configured separately (devicetree) and are not affected.

License: LicenseRef-Nordic-5-Clause (see the accompanying license file).
