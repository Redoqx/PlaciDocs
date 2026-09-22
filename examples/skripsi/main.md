---
title: Rancang Bangun Aplikasi Penulisan Dokumen Berbasis Markdown dan LaTeX
author: Ridho
id: "119140038"
degree: Sarjana Komputer
department: Program Studi Teknik Informatika
faculty: Fakultas Teknologi Industri
institution: Institut Teknologi Sumatera
year: "2026"
style: skripsi-umum
bibliography: references.bib
advisor: Dr. Nama Pembimbing, M.Kom.
# Daftar tambahan milik dokumen ini (yang dibuat tombol "Tambah Daftar").
lists:
  grafik: { title: DAFTAR GRAFIK, prefix: Grafik, label: gfk }
---

# Lembar Pengesahan {pengesahan}

# Abstrak {pembuka}

Penulisan dokumen akademik menuntut kepatuhan pada pedoman format yang rinci.
PlaciDocs memisahkan *isi* dari *aturan penulisan*: penulis cukup menulis dalam
Markdown, sedangkan format diterapkan otomatis oleh mesin LaTeX.

**Kata kunci:** Markdown, LaTeX, penulisan akademik.

# Kata Pengantar {pembuka}

Puji syukur penulis panjatkan atas selesainya skripsi ini.

# DAFTAR ISI {pembuka daftar isi}

# DAFTAR GAMBAR {pembuka daftar gambar}

# DAFTAR GRAFIK {pembuka daftar grafik}

# DAFTAR TABEL {pembuka daftar tabel}

# DAFTAR RUMUS {pembuka daftar rumus}

# Pendahuluan {#sec:pendahuluan}

## Latar Belakang

Sistem penataan huruf TeX dikembangkan oleh @knuth1984 dan kemudian
diperluas menjadi LaTeX [@lamport1994]. Di sisi lain, Markdown dirancang agar
mudah ditulis dan dibaca [@gruber2004, hlm. 1]. Penelitian terdahulu menunjukkan
bahwa kesalahan format merupakan revisi yang paling sering terjadi [-@sommerville2016].

Arsitektur sistem yang diusulkan ditunjukkan pada @fig:arsitektur, sedangkan
perbandingan fitur disajikan pada @tbl:banding.

![Arsitektur PlaciDocs: Markdown, aturan penulisan, dan mesin LaTeX](img/arsitektur.png){#fig:arsitektur width=75%}

## Rumusan Masalah

1. Bagaimana memisahkan isi dokumen dari aturan penulisannya?
2. Bagaimana menerapkan aturan bersyarat, misalnya orientasi halaman?
   - tabel lebar dicetak *landscape*;
   - tabel panjang boleh terpotong antarhalaman.

Pertumbuhan pengguna selama uji coba ditunjukkan pada @gfk:pertumbuhan.

::: Landscape
![Pertumbuhan jumlah pengguna selama uji coba](img/arsitektur.png){#gfk:pertumbuhan width=70%}
::: Landscape

## Tujuan Penelitian

Tujuan penelitian ini adalah membangun aplikasi yang menghasilkan PDF sesuai
pedoman instansi tanpa pengaturan manual.

# Tinjauan Pustaka

## Perbandingan Alat

Tabel: Perbandingan alat penulisan dokumen {#tbl:banding}

| Alat       | Mudah ditulis | Format otomatis | Sitasi |
|:-----------|:-------------:|:---------------:|:------:|
| Word       | Ya            | Tidak           | Plugin |
| LaTeX      | Tidak         | Ya              | Ya     |
| PlaciDocs  | Ya            | Ya              | Ya     |

## Model Tata Letak

Kualitas pemenggalan baris dapat dinyatakan sebagai total *badness*, seperti
pada @eq:badness.

$$
B = \sum_{i=1}^{n} \left( 100 \, r_i^3 \right)
$$ {#eq:badness caption="Total badness pemenggalan baris"}

Skor akhir dihitung dengan @eq:skor.

```math {#eq:skor caption="Skor akhir evaluasi"}
S &= \sum_{j=1}^{m} w_j \, x_j \\
  &= w_1 x_1 + w_2 x_2 + \dots + w_m x_m
```

Rincian kriteria evaluasi yang digunakan disajikan pada @tbl:kriteria.

Tabel: Kriteria evaluasi per aspek {#tbl:kriteria}

| No | Aspek | Bobot | Metode | Sumber | Target | Realisasi | Status |
|---:|:------|------:|:-------|:-------|-------:|----------:|:------:|
| 1  | Margin | 20 | Otomatis | Pedoman | 100 | 100 | Lulus |
| 2  | Huruf  | 15 | Otomatis | Pedoman | 100 | 100 | Lulus |
| 3  | Spasi  | 15 | Otomatis | Pedoman | 100 | 98  | Lulus |
| 4  | Judul  | 25 | Otomatis | Pedoman | 100 | 100 | Lulus |

# Metodologi

## Implementasi

Mesin inti ditulis dalam C++ dan dapat dipanggil dari baris perintah:

```sh
placi build main.md --style skripsi-umum
```

{halaman-baru}

Halaman ini sengaja dimulai di halaman baru dengan baris `\{halaman-baru\}`.

# DAFTAR PUSTAKA {penutup pustaka}

# Data Responden {lampiran}

Tabel: Data responden penelitian {#tbl:responden}

| No | Nama | Usia | Jenis Kelamin | Skor |
|---:|:-----|-----:|:-------------:|-----:|
| 1 | Responden 01 | 21 | L | 4 |
| 2 | Responden 02 | 22 | P | 5 |
| 3 | Responden 03 | 23 | L | 3 |
| 4 | Responden 04 | 24 | P | 4 |
| 5 | Responden 05 | 25 | L | 5 |
| 6 | Responden 06 | 26 | P | 3 |
| 7 | Responden 07 | 20 | L | 4 |
| 8 | Responden 08 | 21 | P | 5 |
| 9 | Responden 09 | 22 | L | 3 |
| 10 | Responden 10 | 23 | P | 4 |
| 11 | Responden 11 | 24 | L | 5 |
| 12 | Responden 12 | 25 | P | 3 |
| 13 | Responden 13 | 26 | L | 4 |
| 14 | Responden 14 | 20 | P | 5 |
| 15 | Responden 15 | 21 | L | 3 |
| 16 | Responden 16 | 22 | P | 4 |
| 17 | Responden 17 | 23 | L | 5 |
| 18 | Responden 18 | 24 | P | 3 |
| 19 | Responden 19 | 25 | L | 4 |
| 20 | Responden 20 | 26 | P | 5 |
| 21 | Responden 21 | 20 | L | 3 |
| 22 | Responden 22 | 21 | P | 4 |
| 23 | Responden 23 | 22 | L | 5 |
| 24 | Responden 24 | 23 | P | 3 |
| 25 | Responden 25 | 24 | L | 4 |
| 26 | Responden 26 | 25 | P | 5 |
| 27 | Responden 27 | 26 | L | 3 |
| 28 | Responden 28 | 20 | P | 4 |
| 29 | Responden 29 | 21 | L | 5 |
| 30 | Responden 30 | 22 | P | 3 |

