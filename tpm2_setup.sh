sudo apt -y update
sudo apt -y install  autoconf-archive libcmocka0 libcmocka-dev procps iproute2 build-essential git pkg-config gcc libtool automake libssl-dev uthash-dev autoconf gnulib doxygen
sudo apt -y install autoconf automake libtool pkg-config gcc libssl-dev libcurl4-gnutls-dev
sudo apt -y install d-bus libdbus-1-dev libglib2.0-dev
 
mkdir tpm-dev
cd tpm-dev
git clone https://github.com/Infineon/eltt2.git
cd eltt2/
make
sudo ./eltt2 -A 616263
cd ..
git clone https://github.com/tpm2-software/tpm2-tss.git
cd tpm2-tss/ .
./bootstrap
ACLOCAL_PATH="/usr/share/gnulib/m4" ./bootstrap
./configure
make
sudo make install
 
cd ..
git clone https://github.com/tpm2-software/tpm2-abrmd.git
sudo useradd --system --user-group tss
cd tpm2-abrmd/ 
./bootstrap
./configure
./configure --with-dbuspolicydir=/etc/dbus-1/system.d
./configure --with-dbuspolicydir=/etc/dbus-1/system.d
sudo make check TESTS=test/integration/auth-session-start-flush.int
./configure --enable-test-hwtpm
sudo make check TESTS=test/integration/auth-session-start-flush.int
make
./configure --with-dbuspolicydir=/etc/dbus-1/system.d
make
sudo make install
sd ldconfig
sudo ldconfig
sudo pkill -HUP dbus-daemon
systemctl daemon-reload
dbus-send --system --dest=org.freedesktop.DBus --type=method_call --print-reply /org/freedesktop/DBus org.freedesktop.DBus.ListNames
 
 
cd ..
git clone https://github.com/tpm2-software/tpm2-tools.git
cd tpm2-tools/
./bootstrap
./configure
make
sudo make install
sudo chown tss:tss /dev/tpm0
tpm2_getrandom 4
cd tpm2-abrmd/
sudo make install
sudo tpm2_getrandom 1
sudo tpm2_getrandom 4
 
#if you get to this point, I’d be interested in getting the output of these commands:
tpm2_pcrlist
tpm2_nvlist
tpm2_nvread --index 0x1c00002 > 0x1c00002.der
tpm2_nvread --index 0x1c0000a > 0x1c0000a.der
openssl x509 -in 0x1c0000a.der -inform der -text
openssl x509 -in 0x1c00002.der -inform der -text
