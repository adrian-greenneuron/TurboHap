# TurboHap 1.5.0-modified.2 (1.5.0 ufficiale modificato): container e HPC

La versione inglese di questo manuale è in `hpc/README.en.md`.

Tre modi di usarlo, con lo stesso programma e gli stessi risultati:

- **A. Immagine Docker** (`turbohap-1.5.0-modified.2-docker.tar.gz`), già pronta.
- **B. Immagine Apptainer** (`turbohap-1.5.0-modified.2.sif`), già pronta, per il cluster.
- **C. Compilazione nativa** sul cluster, per la massima velocità sulla CPU dei nodi.

Le immagini pronte sono compilate per `x86-64-v3` (Intel da Haswell/2013, AMD
Zen e successivi) su base Ubuntu 20.04, quindi funzionano anche con versioni
vecchie di Docker. Se compare "Illegal instruction", la CPU è più vecchia:
ricostruisci con `MARCH=x86-64-v2`.

## Variabili d'ambiente

Lo script di avvio `turbohap-run` legge:

| Variabile | Significato |
| --- | --- |
| `FINDHAP_DIR` | cartella dei dati; gli output vengono scritti lì |
| `OMP_NUM_THREADS` o `FINDHAP_THREADS` | numero di thread (`FINDHAP_THREADS` ha la precedenza) |
| `FINDHAP_LOW_MEMORY=1` | modalità a bassa memoria |
| `FINDHAP_INPUT_VALIDATION=warn` | controllo dei file meno severo (predefinito: `strict`) |
| `FINDHAP_VALIDATE_ONLY=1` | solo controllo degli input, senza imputazione |
| `FINDHAP_CPU_POLICY`, `FINDHAP_ROW_SCHEDULE` | opzioni avanzate (lasciarle non impostate) |
| `fped`, `fgeno`, `fxhsome`, `fparams`, `fgenout`, … | nomi dei file, come nel vecchio container |

`docker run --rm turbohap:1.5.0-modified.2 --help` le elenca tutte.

## A. Docker

```sh
gunzip -c turbohap-1.5.0-modified.2-docker.tar.gz | docker load
docker run --rm -v "$RUNNING_DIR":/home/galaxy/data \
    -e FINDHAP_DIR=/home/galaxy/data -e OMP_NUM_THREADS=16 \
    -e fped=pedigree.file -e fgeno=genotypes.txt -e fxhsome=chromosome.data \
    -e fparams=findhap.options -e fgenout=genotypes.filled \
    --name v15m_chr7_run1 turbohap:1.5.0-modified.2
```

Con il container lasciato attivo:

```sh
docker run -itd -v "$RUNNING_DIR":/home/galaxy/data --entrypoint sleep \
    --name v15m_chr7_run1 turbohap:1.5.0-modified.2 infinity
docker exec -e FINDHAP_DIR=/home/galaxy/data -e OMP_NUM_THREADS=16 v15m_chr7_run1 turbohap-run
docker rm -f v15m_chr7_run1
```

Per ricostruire l'immagine dal sorgente:
`docker build --build-arg MARCH=x86-64-v3 -t turbohap:1.5.0-modified.2 .`

## B. Apptainer

```sh
apptainer run --cleanenv --bind "$RUNNING_DIR":/home/galaxy/data \
    --env FINDHAP_DIR=/home/galaxy/data,OMP_NUM_THREADS=16 \
    turbohap-1.5.0-modified.2.sif
```

Con molte variabili conviene un file (`--env-file turbohap.env`), una per riga
nella forma `NOME=valore`. `--cleanenv` impedisce che entrino variabili del
nodo, come un `OMP_NUM_THREADS` impostato da SLURM.

Per ricostruire il `.sif` dall'immagine Docker:
`apptainer build turbohap.sif docker-archive://turbohap-1.5.0-modified.2-docker.tar`
(dopo `gunzip`). In alternativa `hpc/turbohap.def` costruisce da Rocky Linux 9.

## C. Compilazione nativa

Su un nodo di calcolo della partizione che userai:

```sh
srun -p <partizione> -c 4 --mem=8G --pty bash hpc/build_native.sh
```

Installa in `~/opt/turbohap-1.5.0-modified`. Richiede GCC 9 o più recente, CMake 3.16 o
più recente e Python 3. Su Rocky 8 carica prima un compilatore recente
(`module load gcc` oppure `scl enable gcc-toolset-13 bash`).

## Lanciare i cromosomi con SLURM

Una cartella per cromosoma, ciascuna con i quattro file di input:

```sh
sbatch --array=1-29 --export=ALL,RUN_ROOT=/scratch/$USER/imputazione hpc/run_chromosome.slurm
sbatch --array=1-29 --export=ALL,RUN_ROOT=/scratch/$USER/imputazione,SIF=$HOME/turbohap-1.5.0-modified.2.sif hpc/run_chromosome.slurm
```

`hpc/split_by_chromosome.py` divide i dati del genoma intero per cromosoma;
`hpc/merge_filled.py RUN_ROOT FILE_USCITA` riunisce i `genotypes.filled`
(cartelle `chr1`, `chr2`, … oppure `1`, `2`, … `30`, unite in ordine
numerico); con `--file genotypes.txt` riunisce i genotipi originali.

## Note

- **Controllo degli input.** TurboHap controlla i file prima di partire e si
  ferma se trova errori. Con `FINDHAP_VALIDATE_ONLY=1` puoi controllarli da
  solo, con un rapporto dettagliato. File di aplotipi a priori prodotti dal
  vecchio findhap (con `hap.Xseg` in un altro formato) vengono segnalati come
  errore: vanno rigenerati con TurboHap.
- **Memoria.** Con `maxhap` molto alto la versione modificata riserva molta
  meno memoria della 1.5.0 originale (vedi `MODIFICATIONS.md`).
- **Genoma intero.** La versione modificata può girare anche su tutti i cromosomi
  insieme: con almeno tanti cromosomi quanti thread usa il percorso per
  cromosoma di TurboHap, con meno memoria. Con `maxhap` 1.000.000 mappa
  circa 120 GB di spazio di indirizzi per la libreria, ma usa solo le righe
  occupate; sui cluster con `vm.overcommit_memory = 2` conviene abbassare
  `maxhap` o lavorare per cromosoma.
- **Confronto con la versione originale** sui tuoi dati:
  `python3 tools/compare_builds.py ORIGINALE MODIFICATO cartella --threads 16`.
