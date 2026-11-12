# Limites conhecidos e fora do escopo

- **Fora do escopo da engine**: render, física, áudio, câmera, UI, assets, rede, serialização, editor, threads e gamepad. Render e colisão são do jogo ([games.md](games.md)).
- **Sem host nativo de verdade** (janela, SDL). O núcleo está pronto para isso: o host só chama `frame`, `setHidden` e as funções de input.
- **ECS v1**: sem chunks de tamanho fixo, sem detecção de mudança, sem componentes habilitáveis, sem armazenamento esparso. `ComponentId` vale só dentro do processo.
- **NF-P5** não é atendida a N = 100000 ([performance.md](performance.md)).
- **Duas `Engine`s sobre o mesmo `World`** dividem a tag de escopo: a troca de estado de uma limpa as entidades com escopo da outra. A ponte evita isso criando um `World` por `OlyEngine`.
- **Contornos do arena** (registrados em `games/arena/README.md`): o menu do clique direito é impedido pelo próprio `main.ts`, porque o `attachBrowserInput` só o impede quando `MouseRight` está ligado a uma ação.
