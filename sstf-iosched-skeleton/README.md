# Escalonador de Disco SSTF

Implementação do escalonador de disco SSTF (*Shortest Seek Time First*)
para o Linux Kernel 4.13.9, desenvolvida como parte do Trabalho Prático 2
da disciplina de Laboratório de Sistemas Operacionais.

O projeto utiliza Buildroot e QEMU para executar um sistema Linux
emulado no qual o módulo SSTF e a aplicação de testes são avaliados.

---

## Arquivos principais

- `sstf-iosched.c` — implementação do escalonador SSTF.
- `sector_read.c` — aplicação utilizada para gerar múltiplas requisições
  concorrentes de leitura.
- `Makefile` — regras de compilação do módulo e da aplicação.
- `linuxdistro/buildroot/` — distribuição Linux utilizada para executar
  e testar a implementação.

---

## Ambiente utilizado

- Linux Kernel 4.13.9
- Buildroot 2022.02
- QEMU
- Arquitetura x86
- Disco adicional `/dev/sdb` utilizado nos testes do escalonador

---

# Compilação e execução

Os comandos abaixo consideram o repositório aberto em um GitHub Codespace.

## 1. Preparar o Codespace

No terminal do Codespace:

```bash
cd /workspaces/labsisop-buildroot/linuxdistro/buildroot
```

Instalar as dependências necessárias:

```bash
sudo apt-get update

sudo apt-get install -y \
    qemu-system-x86 \
    cpio \
    bc \
    iputils-ping \
    build-essential \
    rsync \
    file \
    wget \
    unzip \
    python3
```

---

## 2. Compilar o Buildroot

Ainda no diretório:

```text
/workspaces/labsisop-buildroot/linuxdistro/buildroot
```

executar:

```bash
make -j$(nproc)
```

Após a compilação, verificar se as imagens foram geradas:

```bash
ls -lh output/images/bzImage
ls -lh output/images/rootfs.ext2
```

---

## 3. Criar o disco de teste

O teste do escalonador utiliza um segundo disco virtual, que será
identificado dentro do QEMU como `/dev/sdb`.

Criar uma imagem de 1 GiB:

```bash
truncate -s 1G output/images/sdb.img
```

Confirmar:

```bash
ls -lh output/images/sdb.img
```

---

## 4. Iniciar o QEMU

No terminal do Codespace:

```bash
qemu-system-i386 \
    -M pc \
    -kernel output/images/bzImage \
    -drive file=output/images/rootfs.ext2,if=ide,index=0,format=raw \
    -drive file=output/images/sdb.img,if=ide,index=1,format=raw \
    -append "rootwait root=/dev/sda console=ttyS0" \
    -nographic
```

Após o boot, será exibido:

```text
Welcome to Buildroot
buildroot login:
```

Entrar como:

```text
root
```

A partir deste ponto, os comandos são executados **dentro do
Buildroot/QEMU**.

---

# Teste do SSTF

## 5. Verificar o ambiente

Confirmar a versão do kernel:

```bash
uname -r
```

Resultado esperado:

```text
4.13.9
```

Verificar os discos:

```bash
ls -l /dev/sda /dev/sdb
```

```bash
cat /proc/partitions
```

O sistema deve possuir:

```text
/dev/sda
/dev/sdb
```

O `/dev/sda` contém o sistema Buildroot e o `/dev/sdb` é utilizado
para os testes do escalonador.

---

## 6. Verificar os escalonadores disponíveis

```bash
cat /sys/block/sdb/queue/scheduler
```

Antes de carregar o SSTF, uma saída possível é:

```text
noop deadline [cfq]
```

---

## 7. Carregar o módulo SSTF

```bash
modprobe sstf-iosched
```

Verificar novamente:

```bash
cat /sys/block/sdb/queue/scheduler
```

O SSTF deve aparecer entre os escalonadores disponíveis:

```text
noop deadline [cfq] sstf
```

---

## 8. Selecionar o SSTF

```bash
echo sstf > /sys/block/sdb/queue/scheduler
```

Confirmar:

```bash
cat /sys/block/sdb/queue/scheduler
```

Resultado esperado:

```text
noop deadline cfq [sstf]
```

Os colchetes indicam o escalonador atualmente selecionado.

---

## 9. Preparar os logs do kernel

Limpar as mensagens anteriores:

```bash
dmesg -c > /dev/null
```

Habilitar a exibição das mensagens do módulo no console:

```bash
echo 8 > /proc/sys/kernel/printk
```

---

## 10. Executar a aplicação de teste

```bash
sector_read
```

A aplicação cria múltiplos processos para gerar requisições
concorrentes de leitura em diferentes regiões do disco.

Durante a execução, o módulo SSTF registra mensagens como:

```text
[SSTF] add R 147336
[SSTF] dsp R 147336
```

onde:

- `add` representa a chegada de uma requisição à fila;
- `dsp` representa o atendimento de uma requisição.

---

# Coleta dos resultados

## 11. Salvar os registros do SSTF

```bash
dmesg | grep '\[SSTF\]' > /root/sstf.log
```

Verificar a quantidade de mensagens:

```bash
wc -l /root/sstf.log
```

Quantidade de requisições adicionadas:

```bash
grep -c '\[SSTF\] add' /root/sstf.log
```

Quantidade de requisições atendidas:

```bash
grep -c '\[SSTF\] dsp' /root/sstf.log
```

---

## 12. Gerar a sequência de chegada

```bash
awk '$2 == "add" { print $4 }' /root/sstf.log > /root/arrival.txt
```

---

## 13. Gerar a sequência de atendimento

```bash
awk '$2 == "dsp" { print $4 }' /root/sstf.log > /root/dispatch.txt
```

---

## 14. Conferir a quantidade de requisições

```bash
wc -l /root/arrival.txt
wc -l /root/dispatch.txt
```

---

# Cálculo do deslocamento

O deslocamento total é calculado somando a diferença absoluta
entre setores consecutivos de uma sequência.

## Ordem de chegada

```bash
awk '
NR == 1 {
    prev = $1
    next
}
{
    d = $1 - prev
    if (d < 0) d = -d
    total += d
    prev = $1
}
END {
    print "Arrival:", total, "setores"
}' /root/arrival.txt
```

## Ordem de atendimento SSTF

```bash
awk '
NR == 1 {
    prev = $1
    next
}
{
    d = $1 - prev
    if (d < 0) d = -d
    total += d
    prev = $1
}
END {
    print "Dispatch:", total, "setores"
}' /root/dispatch.txt
```

Os dois valores permitem comparar o deslocamento da ordem original
das requisições com o deslocamento produzido pelo SSTF.

---

# Visualização dos dados

Primeiros setores da ordem de chegada:

```bash
head /root/arrival.txt
```

Primeiros setores da ordem de atendimento:

```bash
head /root/dispatch.txt
```

Últimos setores:

```bash
tail /root/arrival.txt
tail /root/dispatch.txt
```

---

# Funcionamento do SSTF

O SSTF mantém uma fila com as requisições de I/O pendentes.

Quando uma nova requisição é recebida, ela é adicionada à fila e
registrada como:

```text
[SSTF] add
```

Quando o escalonador precisa selecionar uma requisição, procura entre
as requisições pendentes aquela cujo setor está mais próximo da posição
de referência atual.

A requisição escolhida é removida da fila e registrada como:

```text
[SSTF] dsp
```

Após o atendimento, a posição de referência é atualizada para o setor
da requisição selecionada.

Dessa forma, o algoritmo procura reduzir o deslocamento acumulado entre
requisições consecutivas.

---

# Avaliação

Como o sistema é executado em um ambiente emulado, medidas de tempo não
são utilizadas como principal critério de comparação.

A avaliação é realizada comparando:

1. a ordem em que as requisições chegam ao escalonador;
2. a ordem em que são atendidas pelo SSTF;
3. o deslocamento total, em setores, das duas sequências.

Esses dados são utilizados para gerar os gráficos e os resultados
apresentados no relatório do trabalho.
