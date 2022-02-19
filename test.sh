eval "make"
eval "g++ zzip.cpp -o zzip"
echo

NAME=output

echo "[zzip] zipping..."
eval "time ./zzip $1 > $NAME.zzip"

echo "[pzip] zipping..."
eval "time ./pzip $1 > $NAME.pzip"
echo

# eval "cat out.zip"
# echo

echo "=== COMPARING ==="
eval "diff $NAME.pzip $NAME.zzip"
DIFF=$(diff $NAME.pzip $NAME.zzip)
if [ "$DIFF" == "" ]; then
	echo "they're the same!"
fi

echo