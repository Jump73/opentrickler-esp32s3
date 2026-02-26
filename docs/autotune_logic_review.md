# Porównanie: obecny autotune vs metodologia OpenTrickler (tuning_guide)

## Cel
Wybrać z obu podejść taki zestaw zasad, który da **maksymalną precyzję bez utraty szybkości**.

## 1) Co wnosi metodologia OpenTrickler (manual)
Najmocniejsze elementy podejścia manualnego:
- Rozdzielenie strojenia COARSE i FINE.
- Twarda zasada: **Ki = 0** (czyli de facto regulator PD), żeby nie kumulować błędu i nie prowokować overshootu.
- Czytelna heurystyka: podnoś Kp do momentu przekroczeń, potem Kd do stłumienia overshootu.
- Kalibracja stop-threshold do realnej rozdzielczości wagi (FINE ≈ dokładność wagi).

Słabsze strony:
- Duża zależność od operatora i powtarzalności warunków.
- Brak automatycznej oceny jakości modelu przepływu między przebiegami.
- Brak formalnej fazy potwierdzania (confirmation) i automatycznych prób przyspieszania.

## 2) Co wnosi obecny autotune ESP32-S3
Najmocniejsze elementy automatyki:
- Koordynatowy search Kp/Kd w log-space (szybkie skoki multiplikatywne i stabilna eksploracja).
- Kryterium wyboru z karą za overshoot + kontrola czasu.
- Fazy: **SEARCH → CONFIRM → SPEED_PROBE**, czyli najpierw trafność, potem stabilność, potem przyspieszanie.
- Warstwa jakości (flow model quality) i odrzucanie słabych runów.
- Stabilizacja zera oraz settle po zatrzymaniu silnika (inercja materiału).

Słabsze strony:
- W komentarzu jest "proportional trickle", ale implementacja jest stała (`min_speed`).
- Część progów jest stała (hardcoded), więc przenoszenie między wagami/prochami wymaga ostrożności.

## 3) Co "wybrać" z obu metod, żeby było najprecyzyjniej i najszybciej

### A. Fundament precyzji (brać z manuala + automatyki)
1. **Zostawić PD bez I** (Ki=0) jako regułę globalną.
2. **FINE stop-threshold ustawiać do realnej dokładności wagi** (z manuala).
3. **Akceptacja runu musi wymagać jednocześnie:**
   - małego błędu masy,
   - małego overshootu,
   - minimalnej jakości modelu.
4. **Obowiązkowy settle po stopie** (już jest) — nie oceniaj trafienia na pierwszym odczycie po zatrzymaniu.

### B. Fundament szybkości (brać głównie z automatyki)
1. Utrzymać fazę **SPEED_PROBE** po potwierdzeniu stabilnego kandydata.
2. Przyspieszać tylko, gdy run jest jednocześnie szybciej **i** dalej w tolerancji masy/overshoot.
3. Nie skracać confirm dla FINE — to etap najbardziej wrażliwy na inercję i szum.

### C. Konkretne korekty, które najbardziej podnoszą precyzję/szybkość
1. **Uspójnić trickle:**
   - albo zmienić komentarz na "fixed min-speed trickle",
   - albo faktycznie zrobić proportional trickle (np. `speed = clamp(kp_trickle*error, min_speed, trickle_max)`).
2. **Zewnętrzne profile progów** (zależne od wagi/prochu), zamiast sztywnych wartości globalnych.
3. **Ujednolicić politykę czasu COARSE/FINE** (jasno opisać dlaczego deadband czasu jest tylko w FINE lub dodać analogiczny mechanizm w COARSE).

## 4) Rekomendowany tryb docelowy (hybryda)
- Manual daje dobre "ramy fizyczne" procesu (progi, brak Ki, separacja coarse/fine).
- Obecny autotune daje skuteczną i powtarzalną automatykę (search/confirm/speed-probe + quality gate).

**Najlepszy wariant praktyczny:**
- zachować obecną automatykę jako silnik strojenia,
- dodać/manualnie wymuszać reguły z tuning guide dla progów i interpretacji wyniku,
- poprawić niespójność trickle i parametryzować progi pod typ wagi/prochu.

To połączenie maksymalizuje jednocześnie:
- **precyzję końcową** (kontrola overshoot + settle + jakość),
- **czas cyklu** (speed probe po stabilnym potwierdzeniu).

## 5) Gdzie dziś faktycznie ustawiasz progi i reguły

### Ustawiane z Web UI (wysyłane do backendu przy starcie autotune)
- `a1` → `coarse_target_weight`
- `a2` → `total_target_time_s`
- `a3` → `fine_target_weight`
- `a5` → `max_runs_per_stage`
- `a6` → `coarse_weight_tolerance`
- `a9` → `fine_weight_tolerance`
- `a7` → `time_tolerance_s`

To znaczy: **główne parametry celu i tolerancji są obecnie sterowane z Web UI**.

### Nieustawiane z Web UI (hardcoded w firmware)
- Progi zatrzymania w samym sterowaniu silnikiem (`stop_threshold` coarse/fine).
- Próg wejścia w tryb trickle (`FINE_TRICKLE_THRESHOLD_GN`).
- Parametry stabilizacji odczytu (okna, odchylenie standardowe, timeouty, min_wait).
- Parametry speed-probe/confirm i część wag/kar w score.

To znaczy: **reguły „wewnętrznej dynamiki” autotune są nadal zakodowane na stałe** i nie są podawane z UI.


## 6) Jak dokładnie te wartości są ustawiane w kodzie (flow end-to-end)
1. Web UI czyta wartości z pól formularza `atParamA1/A2/A3/A5/A6/A9/A7`.
2. Następnie buduje URL:
   `/rest/autotune_coarse?a0=true&a1=...&a2=...&a3=...&a5=...&a6=...&a9=...&a7=...&a8=false&ee=false`.
3. W `rest_autotune_coarse_handler()` backend parsuje parametry query string i wpisuje je do `autotune_request_t`:
   - `a1` -> `request.coarse_target_weight`
   - `a2` -> `request.total_target_time_s`
   - `a3` -> `request.fine_target_weight`
   - `a5` -> `request.max_runs_per_stage`
   - `a6` -> `request.coarse_weight_tolerance`
   - `a9` -> `request.fine_weight_tolerance`
   - `a7` -> `request.time_tolerance_s`
4. Po parsowaniu wywoływane jest `autotune_start(&request)`, które robi walidację zakresów i startuje task autotune.

W skrócie: mapowanie, które podałeś, jest ustawiane bezpośrednio w `components/rest_handlers/rest_handlers.c`, a źródłem tych wartości jest formularz i JS w `html/web_portal.html`.


## 7) Wdrożone zmiany (UI + hybryda)
- Domyślne wartości pól autotune w Web UI zostały zrównane z fallbackami backendu:
  - `a1=37.0`, `a2=20.0`, `a3=40.0`, `a5=15`, `a6=1.00`, `a9=0.02`, `a7=2.0`.
- Trickle został pozostawiony bez zmian (zgodnie z decyzją: nie zmieniamy algorytmu trickle).
- Progi `a6` i `a9` pozostają ustawiane **manualnie przez użytkownika** w Web UI (bez automatycznego nadpisywania heurystyką).

Cel tej zmiany: zachować pełną kontrolę operatora nad tolerancjami i uniknąć niejawnej zmiany wartości wejściowych.
