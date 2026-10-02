# Escalonador de Disco SSTF

Implementação do escalonador de disco SSTF (Shortest Seek Time First)
para o Linux Kernel 4.13.9, desenvolvida como parte do Trabalho Prático 2
da disciplina de Laboratório de Sistemas Operacionais.

## Arquivos

- sstf-iosched.c - implementação do escalonador SSTF.
- sector_read.c - aplicação utilizada para gerar requisições concorrentes
  de leitura.
- Makefile - compilação do módulo e da aplicação de teste.

## Compilação

O projeto utiliza o Buildroot disponível em:

../linuxdistro/buildroot

Para compilar o módulo e a aplicação:

    cd /workspaces/labsisop-buildroot/sstf-iosched-skeleton
    make

Depois, para reconstruir a imagem do Buildroot:

    cd /workspaces/labsisop-buildroot/linuxdistro/buildroot
    make

## Execução

No sistema Buildroot, verificar o disco de teste:

    ls /dev/sdb

Verificar os escalonadores disponíveis:

    cat /sys/block/sdb/queue/scheduler

Carregar o módulo SSTF:

    modprobe sstf-iosched

Selecionar o SSTF:

    echo sstf > /sys/block/sdb/queue/scheduler

Confirmar:

    cat /sys/block/sdb/queue/scheduler

O resultado deve indicar o SSTF como escalonador selecionado.

## Teste

Executar:

    sector_read

A aplicação gera múltiplas requisições concorrentes de leitura no disco.

As mensagens do escalonador podem ser consultadas com:

    dmesg | grep '\[SSTF\]'

São registradas:

- add - chegada de uma requisição.
- dsp - atendimento de uma requisição.

Esses registros permitem comparar a ordem de chegada com a ordem
de atendimento pelo SSTF.

## Funcionamento

O SSTF mantém uma fila de requisições pendentes e registra a posição
atual do disco.

A cada atendimento, é selecionada a requisição cuja posição está mais
próxima da posição atual.

Após o atendimento, a posição atual passa a ser o setor da requisição
atendida.

## Ambiente

- Linux Kernel 4.13.9
- Buildroot
- QEMU
- arquitetura x86
