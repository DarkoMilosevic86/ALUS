# NVDA izvori — referenca, ne deo builda

Ovde su **samo** oni moduli iz NVDA izvornog stabla koji su potrebni kao
izvor istine za ALUS, posle brisanja celog `NVDA/` radnog stabla (2 GB).
Ništa odavde se ne kompajlira i ne instalira.

Preuzeto sa upstream commita:

    84756356cd19a57cfeb4ef265619e54850e06568  (Mon Nov 2 16:49:29 2026 +1100,
                                               "Beta to master (#21102)")

## Šta čemu služi

| Putanja | Koristi |
|---|---|
| `source/globalCommands.py` | telo svake globalne skripte — referenca za pisanje neimplementiranih komandi iz `data/commands-status.ini` |
| `source/browseMode.py`, `cursorManager.py`, `documentBase.py`, `editableText.py`, `virtualBuffers/__init__.py`, `treeInterceptorHandler.py`, `review.py`, `brailleInput.py` | ostali moduli koje skenira `tools/extract-commands.py` (`SCRIPT_MODULES`) |
| `source/inputCore.py`, `scriptHandler.py`, `textInfos/__init__.py` | konstante na koje se gornji moduli pozivaju (`CONSTANT_MODULES`) |
| `source/synthSettingsRing.py`, `synthDriverHandler.py` | semantika prstena podešavanja sinteze (`nextSynthSetting` i porodica) |
| `source/speech/__init__.py`, `ui.py`, `api.py` | šta skripte zapravo izgovaraju i kojim putem |
| `source/controlTypes/role.py`, `state.py` | ulaz za `tools/extract-controltypes.py` |
| `source/keyLabels.py` | ulaz za `data/key-labels.ini` (kako se taster izgovara) |
| `source/locale/*/gestures.ini` | locale sloj mape gestova, 28 jezika; `sr` je obavezan |
| `source/locale/sr/LC_MESSAGES/nvda.po` | ulaz za `tools/import-nvda-po.py` |
| `source/IAccessibleHandler/orderedWinEventLimiter.py` | izvorna lista pravila za `alus::a11y::EventLimiter` (03-RIZICI.md, 01-ARHITEKTURA.md §3.4) |
| `source/compoundDocuments.py` | citirano u 03-RIZICI.md (ControlField sinteza, getTextWithFields) |
| `projectDocs/design/technicalDesignOverview.md` | referenca za ceo 01-ARHITEKTURA.md |
| `UPSTREAM_COMMIT` | zakucan commit; `source_commit_of()` ga čita umesto `git rev-parse`, jer bi git ovde odgovorio ALUS-ovim HEAD-om |

## Pokretanje alata nad ovim stablom

Sva tri alata sada **podrazumevano** čitaju ovo stablo, pa se pokreću bez
argumenata:

    python3 tools/extract-commands.py
    python3 tools/extract-controltypes.py --check
    python3 tools/import-nvda-po.py

Provereno pre brisanja `NVDA/`: `extract-commands.py` daje bajt-identične
`scripts.csv`, `gestures-builtin.ini`, `key-labels.ini` i `locale/sr/gestures.ini`
kao one u `data/` (440 skripti, 390 u opsegu, 320 vezanih tastera, 78 labela
tastera, 28 locale mapa), a `extract-controltypes.py --check` javlja
`unchanged` za svih 6 generisanih fajlova.

Ako nešto zatreba a nije ovde, dodati taj fajl ovde — ne vraćati ceo `NVDA/`.

## Licenca

NVDA je GPL-2.0; ALUS je GPL-2.0-or-later, pa su ovi fajlovi ovde pod svojom
izvornom licencom. Tekst je u `copying.txt`.
