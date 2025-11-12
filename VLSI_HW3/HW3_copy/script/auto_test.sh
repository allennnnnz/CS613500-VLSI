cd ~/VLSI/VLSI_HW3/HW3/src
make clean 
make 
../bin/hw3 ../testcase/public1.lef ../testcase/public1.def ../output/public1.def
../verifier/verify ../testcase/public1.lef ../testcase/public1.def ../output/public1.def