# Safety notice — read before installing

> **Huma Control Center is unofficial software. It changes low-level hardware
> settings of your laptop. Use it at your own risk.** It is provided "as is",
> without any warranty (GPL-2.0-or-later, sections 11 and 12). The authors are
> not responsible for damage to hardware, firmware or data, for lost warranty,
> or for any other consequence of using it.

*Türkçesi aşağıda.*

## What this software does

To give you the settings of the vendor's Windows application on Linux, it:

1. **Loads a third-party kernel module** (`qc71_laptop` fork) that writes to the
   laptop's **embedded controller (EC)** — the chip that controls fans, power
   limits, charging and the keyboard backlight.
2. **Changes CPU power limits (PL1/PL2) and fan tables.** Wrong values can make the
   laptop run hotter, louder, slower, or shut down to protect itself.
3. **Changes battery charging behaviour** (charging profile, charging priority).
4. **Can change one byte of a UEFI firmware variable** (power on when the charger
   is plugged in). This asks for the administrator password, keeps a backup and
   verifies the result, but a failed firmware write is the most serious risk here.
5. **Runs helpers as root** and **uses the webcam** for the optional ambient light
   measurement and presence check (images are processed in memory and never saved).

## Supported hardware

Only the model listed in [`tested-hardware.md`](tested-hardware.md) has been tested:
**Monster Huma H4 V4.1 (Uniwill PH4TUX1), BIOS N.1.15MON06, on Fedora Linux 44.**

- On any other model the driver **refuses to load**. Other laptops can look similar
  and even share the same board, but their firmware may map the same EC addresses
  to different functions.
- `force=1` loads it **read-only** on an unknown model, for diagnostics only.
- `allow_writes=1` (together with `force=1`) enables writes on an unknown model.
  **Do not use it unless you know the EC map of your machine and accept the risk
  of damage.** The kernel is marked as tainted when you do.
- On the supported model with a different BIOS version, power limits and fan tables
  stay read-only until that BIOS is verified.

Please do not bypass these checks to "try it". Run `tools/hw-report.sh` and open a
"New model" issue instead.

## Before you install

- Make sure you can recover: keep a live USB, know how to enter the BIOS setup and
  how to load BIOS defaults, and back up your data.
- If Secure Boot is on, you will enroll a Machine Owner Key (MOK) for the DKMS
  module on the next reboot.
- Do not run the vendor's Windows application and this software against the same
  settings at the same time on dual boot without checking the result; both write to
  the same EC.
- Charging settings and power limits are kept by the EC across reboots and across
  operating systems.

## Built-in safeguards

- Model allowlist (DMI vendor, product, board, SKU and EC project id) in the driver.
- All EC writes pass through one checked function; unknown models are read-only.
- Power limits are fixed to the vendor's values per mode; there is no free input.
- Custom fan curves are validated: thresholds between 30 and 85 °C, rising, fan at
  least 20 % by 70 °C, last step 100 %. The CPU's own thermal throttling and
  shutdown always stay active.
- Raw EC access (debugfs) is off unless `debugregs=1` is given, and root-only.
- The UEFI write is limited to one known byte, with backup, read-back and rollback.
- Uninstall: `install.sh --uninstall` removes the module, services, helpers and settings.
  The UEFI variable backups in `/var/lib/huma-control/` are kept on purpose.
  Settings already stored in the EC stay until changed (e.g. with the vendor app or
  by loading BIOS defaults).

## If something goes wrong

1. Unload the driver: `sudo modprobe -r qc71_laptop` (or uninstall with `install.sh --uninstall`).
2. Shut down completely, unplug the charger, wait a minute and start again; the EC
   resets its runtime state.
3. If fans or charging still behave oddly, load the BIOS defaults, or set the mode
   once with the vendor's Windows application.
4. Report it (without personal data) in an issue, with `tools/hw-report.sh` output
   and `journalctl -b -k | grep qc71`.

---

# Güvenlik uyarısı — kurmadan önce okuyun

> **Huma Control Center resmi olmayan bir yazılımdır ve dizüstü bilgisayarınızın düşük
> seviyeli donanım ayarlarını değiştirir. Kullanım sorumluluğu size aittir.**
> Yazılım "olduğu gibi", hiçbir garanti olmadan sunulur (GPL-2.0-or-later, madde 11
> ve 12). Yazarlar donanımda, yazılımda (firmware) ya da verilerde oluşabilecek
> zarardan, garantinin kaybından ya da kullanımın başka herhangi bir sonucundan
> sorumlu değildir.

**Ne yapar:** Üçüncü taraf bir çekirdek sürücüsü yükleyip dizüstünün gömülü
denetleyicisine (EC: fanlar, güç sınırları, şarj, klavye ışığı) yazar; işlemci güç
sınırlarını ve fan tablolarını, şarj davranışını değiştirir; isteğe bağlı olarak bir
UEFI değişkeninin tek baytını (şarj takılınca açılma) yedek alıp doğrulayarak
değiştirir; root yetkisiyle yardımcılar çalıştırır; ortam ışığı ve "kimse yoksa
kilitle" için kamerayı kullanır (görüntü kaydedilmez).

**Yalnız şu model test edildi:** Monster Huma H4 V4.1 (Uniwill PH4TUX1), BIOS
N.1.15MON06, Fedora Linux 44. Başka bir modelde sürücü **yüklenmez**. `force=1`
yalnız okuma içindir; `allow_writes=1` ile yazmayı açmayın — aynı anakartı kullanan
başka bir modelde aynı adres başka bir işe yarıyor olabilir. Farklı BIOS sürümünde
güç sınırları ve fan tabloları salt okunur kalır. Modelinizi eklemek için
`tools/hw-report.sh` çıktısıyla "New model" kaydı açın.

**Kurmadan önce:** verilerinizi yedekleyin; canlı USB'niz olsun ve BIOS'a girip
varsayılanları yüklemeyi bilin. Secure Boot açıksa yeniden başlatmada MOK anahtarı
kaydedeceksiniz. Çift sistemde üreticinin Windows uygulamasıyla aynı ayarları aynı
anda değiştirmeyin; ikisi de aynı denetleyiciye yazar. Şarj ve güç ayarları
denetleyicide kalır, işletim sistemi değişse de geçerlidir.

**Bir sorun olursa:** `sudo modprobe -r qc71_laptop` ya da `install.sh --uninstall`; sonra
bilgisayarı tamamen kapatıp şarjı çıkarın, bir dakika bekleyip açın. Sürerse BIOS
varsayılanlarını yükleyin ya da modu bir kez üreticinin Windows uygulamasıyla
ayarlayın. Kişisel veri içermeden `tools/hw-report.sh` çıktısı ve
`journalctl -b -k | grep qc71` ile bildirin.
