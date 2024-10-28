# Athena_PG


This is the repository of Athena_PG which implments the order-centric plan explorer of [Athena](https://anonymous.4open.science/r/Athena-D645).

## Dependencies

```bash 
apt-get update

apt-get install -y wget unzip build-essential pkg-config libicu-dev libreadline-dev libz-dev bison flex gosu locales
```
## Build
### 1. Build Postgres
```bash
# configure pg
./configure --prefix=<set you dir for postgres>/pg 

# build
make -j "$(nproc)" install 

# add system path
echo 'export PATH="$PATH:/<you dir for postgres>/pg/bin"' >> ~/.bashrc
source ~/.bashrc

# enter pg dir
cd <you dir for postgres>/pg

# init postgres data dir
initdb -D data

# start postgres
pg_ctl -D data -l logfile start

# connect to postgres
psql postgres
```

### 2. Build pg_prewarm
```bash
cd contrib/pg_prewarm

make install
```

### 3. Build pg_hint_plan
```bash
wget -O REL16_1_6_1.tar.gz "https://github.com/ossc-db/pg_hint_plan/archive/refs/tags/REL16_1_6_1.tar.gz"

tar -zxvf REL16_1_6_1.tar.gz

cd pg_hint_plan-REL16_1_6_1/

# you need to modify the line 4 of file SPECS/pg_hint_plan16.spec
"%define _pgdir   /usr/pgsql-16" => "%define _pgdir   <you dir for postgres>/pg"

make install
```

### 4. Load pg_prewarm and pg_hint_plan
```bash
# connect to postgres
psql postgres

postgres=# LOAD 'pg_hint_plan';
postgres=# CREATE EXTENSION pg_prewarm;
```

