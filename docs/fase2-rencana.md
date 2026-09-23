# PlaciDocs — Rencana Fase 2

Status (2026-09-22): 2A (engine) dan 2B (GUI) sudah diimplementasikan dan diuji. Lihat bagian 5 untuk yang belum dikerjakan.

## 1. Dua jenis penanda

PlaciDocs memakai dua mekanisme yang sengaja dibedakan:

| | **Tag judul** `{...}` | **Section format** `::: Nama ... ::: Nama` |
|---|---|---|
| Menempel pada | akhir judul (`#`, `##`, `###`, …) | blok teks bebas, dibuka dan ditutup dengan nama yang sama |
| Mengatur | peran dan format *bab/sub-bab* itu: tanpa nomor, bagian pembuka/penutup, isi yang dibangkitkan (daftar isi, daftar pustaka) | tata halaman seperti *section break* di Word: orientasi, margin, kolom, nomor halaman |
| Cakupan | judul tersebut + isinya sampai judul berikutnya yang levelnya sama atau lebih tinggi | persis di antara kedua penanda |
| Didefinisikan di | style (`tags:`) | style (`sections:`) |

### 1.1 Tag judul

```markdown
# Kata Pengantar {pembuka -}
# Daftar Isi {daftar isi}
# Daftar Gambar {daftar gambar}
# Pendahuluan {#sec:intro}
# Daftar Pustaka {pustaka IEEEStyle}
# Kuesioner {lampiran}
```

Isi kurung kurawal adalah token yang dipisah spasi:

| Token | Arti |
|---|---|
| `-` | tanpa nomor (bawaan) |
| `#label` | label untuk rujukan silang (bawaan) |
| `kunci=nilai` | atribut (bawaan) |
| kata lain | nama tag yang didefinisikan di style, misalnya `pembuka`, `IEEEStyle` |

Contoh definisi di style:

```yaml
tags:
  pembuka:   { aliases: [front], numbered: false, page_numbering: { style: roman } }
  penutup:   { aliases: [back],  numbered: false }
  lampiran:  { aliases: [appendix], label: "LAMPIRAN {num}", numbering: "{Alpha}" }
  IEEEStyle: { bibliography: { style: ieeetr, citation: numeric } }
```

Tag bawaan, masing-masing dengan alias Indonesia dan Inggris:
- `daftar <nama>` / `list <nama>`: membangkitkan daftar dari `lists:`. `daftar isi` / `list contents` adalah daftar isi.
- `pustaka` / `bibliography`: membangkitkan daftar pustaka di bawah judul itu.

Paper yang tidak punya pembuka dan penutup tidak perlu tag sama sekali.

**Pembatas tag bisa diganti** di style, untuk dokumen yang judulnya memang memuat kurung kurawal:

```yaml
syntax:
  tag_open: "{{"
  tag_close: "}}"
```

Kurung kurawal harfiah di judul juga bisa ditulis `\{home\}`.

### 1.2 Section format

````markdown
::: JadiLandscape
| tabel lebar ... |
::: JadiLandscape
````

- Nama wajib ditulis di pembuka dan penutup, dan keduanya harus sama. Section boleh bersarang.
- Nama-nama section didefinisikan di style:

```yaml
sections:
  Landscape:     { aliases: [JadiLandscape], orientation: landscape }
  DuaKolom:      { columns: 2 }
  MarginSempit:  { margin: { left: 2cm, right: 2cm }, font_size: 11pt }
```

- Rule bersyarat tetap bisa merujuknya: `when: { element: table, in: DuaKolom }`.
- Rule otomatis (misalnya tabel > 6 kolom → landscape) tetap berjalan tanpa section.

### 1.3 Daftar dinamis

```yaml
lists:
  gambar: { aliases: [figures], title: DAFTAR GAMBAR, prefix: Gambar, label: fig, numbering: "{h1}.{n}" }
  tabel:  { aliases: [tables],  title: DAFTAR TABEL,  prefix: Tabel,  label: tbl }
  rumus:  { aliases: [equations], title: DAFTAR RUMUS, prefix: Rumus, label: eq, numbering: "({h1}.{n})" }
```

- Sebuah item masuk ke daftarnya lewat awalan label, misalnya `{#eq:x}` atau `{#gfk:penjualan}`.
- Daftar yang dibuat lewat tombol **Tambah Daftar** disimpan di front matter dokumen, dengan format yang sama.

### 1.4 Rumus

- `$...$` (dalam kalimat) dan `$$...$$ {#eq:x}` (bernomor) tetap berlaku.
- Tambahan: blok ```` ```math {#eq:x caption="..."} ```` untuk rumus multi-baris, yang otomatis masuk Daftar Rumus.

## 2. Langkah eksekusi

### 2A. Engine
1. **Cache Tectonic dibundel**: target CMake `texcache` menjalankan dokumen cakupan dengan `TECTONIC_CACHE_DIR=build/texcache`. Runtime selalu memakai cache itu dengan `--only-cached`, dan skrip `-Warm` dihapus.
2. **Parser**:
   - Blok `{...}` di akhir judul dengan pembatas yang bisa diganti dan escape `\{`.
   - `::: Nama` berpasangan (penutup harus sama, jika tidak muncul error berisi nomor baris).
   - Posisi baris sumber di setiap node.
3. **Style**: `tags:`, `sections:`, `lists:`, `syntax:`, alias. Loader memvalidasi referensi tag/section yang tidak dikenal.
4. **Emitter**:
   - Format per bab dari tag: ketika format berganti, emitter menulis ulang titleformat, penomoran halaman, dan pagestyle di titik itu.
   - Section: `\newgeometry`, `landscape`, `multicols`.
   - Daftar dinamis: `newfloat` untuk jenis float, counter + `\addcontentsline` untuk rumus.
5. **Migrasi**: `::: frontmatter`, `::: toc`, dan sejenisnya dihapus. Contoh dan test disesuaikan.

### 2B. GUI (Qt 6.8 LTS, MinGW, `libplaci`)
- **Editor** di kiri dengan syntax highlighting (tag dan section diberi warna khusus) dan outline bab.
- **Preview** di kanan berupa PDF asli (`QPdfView`), dikompilasi ulang di background sekitar 1 detik setelah berhenti mengetik. Klik PDF ↔ baris MD lewat SyncTeX.
- **Toolbar**:
  - Format paragraf/judul.
  - Tag untuk judul aktif.
  - Section: membungkus teks terpilih.
  - Sisip Gambar, Tabel, Rumus.
  - Sitasi (pencarian di `.bib`) dan rujukan silang.
  - **Daftar ▾**: daftar yang ada + **Tambah Daftar…**.
- **Status bar** berisi diagnostik yang bisa diklik untuk lompat ke baris.

## 3. Verifikasi
- Unit test untuk: tag + cakupan, pembatas kustom, escape, `:::` yang tidak berpasangan atau salah nama, daftar dinamis, rumus.
- Build dua dokumen contoh:
  - skripsi: pembuka/penutup lewat tag, section landscape;
  - paper tanpa tag.
- Build dengan jaringan dimatikan.
- Uji GUI manual.

## 4. Keputusan (2026-09-22)
- **Page break**: satu baris berisi `{halaman-baru}` (alias `{pagebreak}`), memakai pembatas tag yang aktif.
- **Sampul/pengesahan**: tag judul yang di style punya `template:` (LaTeX, diisi `{{kunci}}` dari front matter) dan `hide_title: true`. Tag bawaan `sampul`/`cover` memakai `cover:` dari style. Bila dokumen tidak memakainya, sampul dibuat otomatis di awal.
- **Urutan**: 2A (engine) lalu langsung 2B (GUI).
- Qt 6.8.3 LTS (MinGW 13.1), karena `aqtinstall` belum bisa membaca repositori Qt 6.11.

## 5. Status dan sisa pekerjaan (2026-09-23)
Sudah selesai:
- Tag judul (dengan pembatas yang bisa diganti dan escape), section berpasangan, `{halaman-baru}`.
- Daftar dinamis (`newfloat`), rumus bercaption, template `pengesahan`, pustaka bertag.
- Cache LaTeX dibundel (±50 MB, `--only-cached`), dengan fallback unduh sekali plus peringatan.
- Aplikasi Qt: editor, kerangka, preview live yang mengikuti kursor, panel masalah,
  toolbar Tag/Section/Daftar/Rujukan/Sisipkan, Tambah Daftar, dokumen terakhir, simpan otomatis.
- Error LaTeX dipetakan ke baris Markdown (peta baris `.tex` → `.md` dari emitter).
- Test: 40 kasus engine + 11 kasus GUI (dialog dijalankan headless lewat `QT_QPA_PLATFORM=offscreen`).
- Paket instalasi satu folder (`cmake --install`) dan ZIP lewat CPack.

Belum dikerjakan:
- SyncTeX: klik di PDF lalu lompat ke baris. Saat ini sinkronisasi satu arah, lewat bookmark per judul.
- Peringatan LaTeX (misalnya *overfull hbox*) belum ikut dipetakan; baru error yang dipetakan.
- Installer berbasis NSIS/MSI (sekarang baru ZIP) dan penandatanganan biner.
- Penyuntingan berkas style lewat antarmuka.
