#!/bin/sh
echo -e '#!/bin/sh\n cd ~/csc369/root;os161-gdb kernel;' > debugg.sh;
chmod +x debugg.sh;
gnome-terminal -x ./debugg.sh;
sys161 -w kernel;
