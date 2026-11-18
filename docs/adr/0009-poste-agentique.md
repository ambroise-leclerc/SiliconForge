# ADR 0009 — Réunir terminal d’agent et revue des sources

- Statut : proposée
- Date : 2026-11-18

## Contexte et état de l’art

Les quatre agents visés proposent déjà une interface terminal. OpenCode démarre dans le répertoire du projet. Codex, Mistral Vibe et Claude Code distribuent également leur propre client. Cette interface commune permet de conserver les commandes, les raccourcis et la configuration habituels de chaque utilisateur. [OpenCode](https://opencode.ai/docs/cli/tui/) · [Codex CLI](https://github.com/openai/codex) · [Mistral Vibe](https://github.com/mistralai/mistral-vibe) · [Claude Code](https://code.claude.com/docs/en/overview)

## Décision

1. Le terminal intégré lance le client installé par l’utilisateur dans le répertoire du projet. Un pseudo-terminal natif (PTY sur Unix, ConPTY sur Windows) transmet saisie, redimensionnement et interruption. Le client gère sa connexion au fournisseur. La vue du banc demeure utilisable après la fermeture du client.
2. Une fenêtre distincte ouvre les fichiers du même répertoire, affiche les différences Git et permet de modifier et sauvegarder le code. Une modification externe recharge un fichier propre. Si le tampon contient des modifications locales, l’éditeur présente les deux versions avant toute écriture.
3. L’agent accède au banc par une commande locale scriptable. L’éditeur et le terminal peuvent donc évoluer indépendamment du moteur de simulation.

## Alternatives et conséquences

Intégrer séparément quatre SDK donnerait accès à des événements de conversation structurés, au prix de quatre intégrations à maintenir. Le terminal impose en échange de qualifier le rendu interactif sur macOS, Linux et Windows. Un terminal externe reste pratique en développement mais disperse code, session et mesures entre plusieurs fenêtres indépendantes.

La qualification couvrira le lancement des quatre clients, la saisie interactive, le redimensionnement, l’interruption et la relecture d’un changement concurrent sans perte de texte.
