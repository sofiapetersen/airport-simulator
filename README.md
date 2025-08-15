# Simulador de Tráfego Aéreo

Este é um simulador de controle de tráfego aéreo desenvolvido em C. O programa utiliza multithreading para gerenciar múltiplos aviões (domésticos e internacionais) que competem por recursos limitados do aeroporto: pistas, portões de embarque e a torre de controle.

O objetivo é simular um ambiente complexo, implementando mecanismos para evitar problemas de concorrência, como deadlock e starvation.

## Funcionalidades Principais

* **Simulação Concorrente**: Cada avião é representado por uma thread, permitindo operações simultâneas.
* **Gerenciamento de Recursos**: Controla o acesso a pistas, portões e torres de controle usando mutex e variáveis de condição.
* **Sistema de Prioridades**: Implementa uma fila de prioridade para alocação de recursos, evitando que aviões esperem indefinidamente (starvation).
* **Detecção de Deadlock**: Um monitor dedicado analisa o grafo de espera de recursos para detectar e resolver deadlocks, cancelando o voo de uma das threads "vítimas".
* **Interface em Tempo Real**: Utiliza a biblioteca `ncurses` para exibir um painel com o status dos recursos, a lista de aviões ativos e as filas de espera.
* **Log e Estatísticas**: Ao final da simulação, exibe um log detalhado de todas as operações e um resumo estatístico com o número de voos bem-sucedidos, falhas, e os motivos das falhas (timeout, deadlock).

## Pré-requisitos

Para compilar e executar o projeto, você precisa ter:
* Um compilador C (como o GCC).
* A biblioteca `ncurses` instalada.
* A biblioteca `pthreads`.

Na maioria dos sistemas baseados em Linux, você pode instalar a `ncurses` com o seguinte comando:
```bash
sudo apt-get install libncurses-dev
```

## Como Compilar

Abra o terminal na pasta onde o arquivo trabalho.c está localizado e execute o seguinte comando:
```bash
gcc -g -Wall trabalho.c -o trabalho -lncursesw -pthread
```

- gcc -g trabalho.c: Compila o código-fonte.

- -Wall: habilita todas as mensagens de aviso do compilador.

- -o trabalho: Define o nome do arquivo executável como simulador.

- -pthread: Vincula a biblioteca de Pthreads.

- -lncursesw: Vincula a biblioteca Ncurses.

## Como Executar
Existem duas maneiras de executar o simulador:

### Modo Interativo
Neste modo, o programa solicitará que você insira os parâmetros da simulação.

```bash
./simulador
```
Você deverá informar o número de pistas, portões, torres e o tempo total da simulação pelo terminal.

### Modo Automático (com Argumentos)
Você pode passar os parâmetros da simulação diretamente pela linha de comando.

```bash
./simulador <pistas> <portoes> <torres> <tempo_segundos>
```

Exemplo:
```bash
./simulador 3 5 2 120
```
Isso iniciará uma simulação com:

- 3 Pistas
- 5 Portões
- 2 Operações na Torre de controle
- Duração de 120 segundos

#

Desenvolvido por João Vitor Laimer e Sofia Petersen