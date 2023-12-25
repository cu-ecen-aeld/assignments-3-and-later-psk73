logger "TESTIN-$1"
echo "TEST $1"
echo "loading aesdchar device"
./aesdchar_load
echo "wring start---------------"
for i in {1..20}
do
	echo "writing $i"
	echo "Test $1 $i" > /dev/aesdchar
done
echo "wring done---------------"
sleep 2
echo "reading start------------------"
cat /dev/aesdchar
echo "reading end------------------"
sleep 2
echo "unloading aesdchar device"
./aesdchar_unload
