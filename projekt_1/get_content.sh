#!/usr/bin/env bash

set -e

URL="https://download.geofabrik.de/europe/poland-latest.osm.pbf
FILE="assets/poland-latest.osm.pbf"

echo "Ten skrypt pobierze mapę Polski OpenStreetMap."
echo "Rozmiar pliku to około 1.9 GB."
echo

read -p "Czy chcesz kontynuować? [t/N] " ANSWER

case "$ANSWER" in
    [tT]|[tT][aA][kK]|[yY]|[yY][eE][sS])
        ;;
    *)
        echo "Anulowano."
        exit 0
        ;;
esac

echo
echo "Pobieranie $FILE..."

wget -c -O "$FILE" "$URL"

echo
echo "Pobieranie zakończone."

SIZE=$(du -h "$FILE" | cut -f1)

echo "Zapisano jako: $FILE"
echo "Rozmiar: $SIZE"