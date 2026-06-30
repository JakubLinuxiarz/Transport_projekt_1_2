# Transport_projekt_1_2

Repozytorium zawiera dwa niezależne projekty napisane w C++20.

---

# Projekt nr 1 – Router Drogowy Polski

Aplikacja do wyszukiwania najkrótszych tras samochodowych na podstawie danych OpenStreetMap (`.osm.pbf`).

Program:

- przetwarza mapę OSM do postaci grafu,
- wyszukuje miejscowości po nazwie,
- oblicza trasę pomiędzy punktami,
- umożliwia porównanie różnych algorytmów,
- zapisuje przebieg działania algorytmów,
- pozwala wizualizować odwiedzane wierzchołki.

## Zaimplementowane algorytmy

- Dwukierunkowa Dijkstra
- Dwukierunkowe A*
- Wielowątkowa Dijkstra
- Wielowątkowe A*

## Wymagania

```bash
sudo apt install g++ make \
    zlib1g-dev libbz2-dev libexpat1-dev \
    libsfml-dev
```

Program wymaga też pobrania mapy do folderu assets. Można to zrobić za pomocą:

```bash
./get_content.sh
```

## Kompilacja

```bash
make
```

## Uruchomienie

Router:

```bash
./bin/router
```

Wizualizacja:

```bash
./bin/visualizer
```

Przykład automatycznego uruchomienia:

```bash
./example.sh
```

---

# Projekt nr 2 – Kraków MPK Router

Aplikacja do wyszukiwania połączeń komunikacji miejskiej w Krakowie na podstawie danych GTFS udostępnianych przez MPK Kraków.

Program buduje graf zdarzeń, w którym każdy wierzchołek reprezentuje przyjazd lub odjazd pojazdu na konkretnym przystanku o określonej godzinie.

## Funkcjonalności

- wyszukiwanie połączeń pomiędzy przystankami,
- uwzględnianie przesiadek,
- obsługa kursów nocnych,
- wyszukiwanie połączeń dla wybranej daty,
- graficzny interfejs użytkownika.

## Dostępne tryby

- Najwcześniejszy dojazd
- Zbalansowany
- Najmniej przesiadek
- Najpóźniejszy wyjazd

## Wymagania

```bash
sudo apt install g++ make unzip libsfml-dev
```

## Kompilacja

```bash
make
```

## Uruchomienie

```bash
./bin/krk_router
```

---

# Technologie

- C++20
- SFML
- OpenStreetMap
- GTFS
- Algorytmy grafowe
- Programowanie wielowątkowe