# PlaciDocs

Tulis dalam Markdown, dapatkan PDF yang mengikuti aturan penulisan instansi Anda.

PlaciDocs memisahkan **isi** (file `.md`) dari **aturan penulisan** (file style `.yaml`).
Penulis tidak perlu memikirkan margin, huruf, spasi, penomoran bab dan halaman, gambar,
maupun tabel. Semua itu ditentukan oleh style lalu dicetak oleh mesin LaTeX (Tectonic)
yang ikut dibundel **beserta paket-paketnya**, sehingga aplikasi bisa dipakai offline.

Status: **Fase 2** sudah berjalan, yaitu engine dengan tag, section, dan daftar dinamis,
ditambah aplikasi desktop Qt dengan live preview. Rencana dan keputusan desain ada di
[`docs/fase2-rencana.md`](docs/fase2-rencana.md).

## Isi repositori

| Folder | Isi |
|---|---|
| `src/core/parser` | preprocessor (front matter, `::: Nama`, `{halaman-baru}`, escape `\{`) + md4c → AST (dengan nomor baris) |
| `src/core/passes` | tag judul, figure, caption tabel, rumus, sitasi, rujukan silang |
| `src/core/style` | struct `Style` + loader YAML (`tags`, `sections`, `lists`, `syntax`, `extends`) |
| `src/core/rules` | aturan bersyarat (`when`/`set`, termasuk `in: <tag atau section>`) |
| `src/core/latex` | preamble dan emitter (format per tag, nomor halaman, daftar dinamis, section) |
| `src/core/compile` | pemanggil Tectonic dengan cache paket yang dibundel |
| `src/cli` | `placi`, aplikasi baris perintah |
| `src/gui` | `placidocs`, aplikasi desktop Qt 6 |
| `styles/` | style bawaan: `default`, `skripsi-umum`, `laporan-kantor` |

## Build

Kebutuhan: CMake ≥ 3.20 dan compiler C++20. Semua library ada di `third_party/`.

```powershell
pwsh scripts/fetch-tectonic.ps1                    # sekali: biner Tectonic
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --build build --target texcache              # sekali: paket LaTeX ke build/texcache (±50 MB)
./build/placi_tests
```

**Aplikasi desktop.** Konfigurasikan dengan compiler yang sama dengan yang dipakai untuk
membangun Qt. Contoh berikut untuk Qt 6.8 MinGW:

```powershell
cmake -S . -B build-qt -G Ninja -DCMAKE_BUILD_TYPE=Release `
      -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64 `
      -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
      -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe
cmake --build build-qt --target placidocs          # windeployqt menyalin DLL Qt ke build-qt/
cmake --build build-qt --target texcache           # atau salin build/texcache ke build-qt/
./build-qt/placidocs examples/skripsi/main.md
```

**Offline.** `placi` dan `placidocs` mencari `texcache/` di sebelah executable, lalu
menjalankan Tectonic dengan `--only-cached`. Jika suatu dokumen ternyata butuh berkas
yang tidak ada di cache (misalnya paket dari `preamble:` sebuah style), berkas itu
diunduh sekali disertai peringatan. `placi build --online` mengizinkan unduhan secara eksplisit.

## Pemakaian CLI

```sh
placi build examples/skripsi/main.md            # -> examples/skripsi/main.pdf
placi build doc.md -s laporan-kantor -o out.pdf # pilih style lewat CLI
placi tex doc.md -o doc.tex                     # hanya LaTeX
placi check-style styles/skripsi-umum.yaml      # validasi style (error per baris)
placi ast doc.md                                # AST + hasil rule, untuk debugging
placi styles                                    # daftar style bawaan
```

`placi.exe` adalah program terminal. Kalau di-double-click, jendelanya langsung tertutup.
Untuk menulis dengan tampilan, gunakan `placidocs.exe`.

## Dialek Markdown PlaciDocs

Semua sintaks CommonMark + GFM (tabel, coret) berlaku. Ada dua jenis penanda tambahan:

**1. Tag judul `{...}`, ditulis di akhir judul.** Tag berlaku untuk judul itu beserta
isinya sampai judul berikutnya yang setingkat atau lebih tinggi. Isi pokok tidak perlu
diberi tag; paper biasa tidak memakai tag sama sekali.

```markdown
# Lembar Pengesahan {pengesahan}          ← template dari style
# Kata Pengantar {pembuka}                ← bagian pembuka: tanpa nomor, halaman romawi
# DAFTAR ISI {pembuka daftar isi}
# DAFTAR GAMBAR {pembuka daftar gambar}
# Pendahuluan {#sec:intro}                ← isi pokok, hanya label
# Ucapan Terima Kasih {-}                 ← tanpa nomor
# DAFTAR PUSTAKA {penutup pustaka}
# Kuesioner {lampiran}                    ← LAMPIRAN A, B, ...
```

- Token bawaan: `-` (tanpa nomor), `#label`, `kunci=nilai`, `daftar <nama>` / `list <nama>`,
  `pustaka` / `bibliography`, `sampul` / `cover`. Kata lain adalah tag dari `tags:` di style.
- Kurung kurawal harfiah ditulis `\{home\}`. Pembatasnya bisa diganti di style lewat
  `syntax: { tag_open: "[[", tag_close: "]]" }`.

**2. Section format `::: Nama … ::: Nama`**, seperti *section break* di Word: mengubah tata
halaman (orientasi, margin, kolom, ukuran huruf). Nama wajib ditulis di pembuka dan
penutup, dan nama-nama section didefinisikan di `sections:`.

```markdown
::: Landscape
| tabel yang sangat lebar ... |
::: Landscape
```

**Isi lainnya**

| Kebutuhan | Tulis |
|---|---|
| Gambar bernomor | `![Caption](img/a.png){#fig:a width=70%}` (sendirian dalam satu paragraf) |
| Caption tabel | `Tabel: Caption {#tbl:x}` tepat di atas/bawah tabel |
| Rumus | `$x^2$`, `$$ ... $$ {#eq:x}`, atau blok ```` ```math {#eq:x caption="..."} ```` (masuk Daftar Rumus) |
| Sitasi | `[@kunci]`, `[@a, hlm. 5; @b]`, `@kunci` (naratif), `[-@kunci]` (tahun saja) |
| Rujukan silang | `@fig:a` → "Gambar 1.1", `@tbl:x`, `@eq:x`, `@sec:intro`, `@gfk:x` (daftar buatan sendiri) |
| Pindah halaman | satu baris berisi `{halaman-baru}` |
| LaTeX mentah | blok kode ```` ```latex ```` |

**Daftar dinamis.** Setiap daftar (gambar, tabel, rumus, atau buatan sendiri) memiliki
judul, awalan caption, awalan label, dan format nomor. Item masuk ke sebuah daftar lewat
awalan labelnya. Daftar khusus dokumen ditulis di front matter; inilah yang dibuat
oleh tombol **Tambah Daftar** di aplikasi:

```yaml
lists:
  grafik: { title: DAFTAR GRAFIK, prefix: Grafik, label: gfk }
```

## File style

Lihat `styles/skripsi-umum.yaml` (lengkap dengan komentar). Bagian utamanya:
`page`, `font`, `paragraph`, `headings` (h1–h6 + `all`), `page_numbering`,
`figure`, `table`, `lists`, `tags`, `sections`, `bibliography`, `cover`, `syntax`,
`rules`, dan `extends` (mewarisi style lain).

```yaml
tags:
  pembuka:  { aliases: [front], numbered: false, page_numbering: { style: roman, position: bottom-center } }
  lampiran: { aliases: [appendix], label: "LAMPIRAN {num}", numbering: "{Alpha}" }
  IEEEStyle: { bibliography: { style: ieeetr, citation: numeric } }
sections:
  Landscape: { aliases: [JadiLandscape], orientation: landscape }
  DuaKolom:  { columns: 2 }
rules:
  - when: { element: table, columns: { gt: 6 } }
    set:  { page.orientation: landscape }
  - when: { element: table, in: lampiran }
    set:  { table.font_size: 10pt }
```

## Aplikasi desktop

Tampilannya terdiri dari editor dengan penyorotan sintaks, kerangka dokumen, dan preview
PDF asli yang dikompilasi ulang sekitar 1 detik setelah Anda berhenti mengetik. Preview
mengikuti bagian tempat kursor berada, dan panel Masalah bisa diklik untuk lompat ke baris.

Toolbar menyediakan:
- format judul/paragraf;
- **Tag** (untuk judul yang sedang aktif);
- **Section** (membungkus teks terpilih);
- sisip Gambar, Tabel, Rumus, dan Sitasi (dari `.bib`, termasuk formulir referensi baru);
- **Rujukan** (dari label yang ada);
- **Halaman baru**;
- **Daftar ▾** berisi daftar yang tersedia dan **Tambah Daftar…**;
- pilihan Style dan Ekspor PDF.

Untuk menelusuri masalah build di aplikasi, set variabel `PLACI_LOG=<berkas>`.

## Lisensi

PlaciDocs: GPLv3 (lihat `LICENSE`).

Pihak ketiga:

md4c (MIT), rapidyaml (MIT), CLI11 (BSD-3), doctest (MIT), Tectonic (MIT), Qt 6 (LGPLv3).
