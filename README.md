# bstep

Un interpréteur visuel pour les exercices de C. Le triangle dans la marge indique la prochaine instruction. La mémoire montre les variables, les cases des tableaux et les cibles des pointeurs. L’éditeur et les flux utilisent les couleurs et le gras ANSI. L’interface LibLapin utilise des atlas de glyphes PNG.

## Compiler et ouvrir

Sous Linux, utilise Make avec une installation de LibLapin où b++ est disponible dans le PATH. Garde le fichier `bstep-gui` et les dossiers `assets` et `programmes` à côté de `bstep`.

    make
    ./bstep

Le dossier source par défaut est `~/.bstep/programmes` s'il existe. Une copie de travail utilise son dossier `programmes` adjacent. Une installation crée `~/.bstep/programmes` au premier lancement pour servir de dossier source et d'enregistrement. Un dossier passé en argument devient la racine de l’explorateur. Si tu passes un fichier, l’application l’ouvre et prend son dossier parent comme racine.

    ./bstep mon-dossier-c
    ./bstep mon-dossier-c/exercice.c
    ./bstep --fenetre mon-dossier-c

## Paquet Debian

Configure dans APT le dépôt qui fournit `liblapin-dev` avec `b++` et SFML 3 pour la distribution cible. Depuis la racine des sources, installe les dépendances et construis le paquet.

```sh
sudo apt install build-essential debhelper liblapin-dev
dpkg-buildpackage -us -uc -b
```

Le paquet `bstep_<version>_<architecture>.deb` se trouve dans le répertoire parent, prêt à importer dans le dépôt APT. Renseigne la version et la distribution de ton dépôt dans `debian/changelog`. Une fois le paquet importé et l'index du dépôt mis à jour, `apt install bstep` installe les deux interfaces.

| Emplacement | Contenu |
| --- | --- |
| `/usr/bin/bstep` | Commande à lancer. |
| `/usr/lib/bstep/bstep-gui` | Exécutable graphique utilisé par bstep. |
| `/usr/share/bstep/` | Atlas, exemples dans `programmes/`, parcours et banque complémentaire. |
| `~/.bstep/programmes/` | Programmes personnels et sauvegardes. |
| `~/.bstep/parcours/` | Parcours personnels ajoutés au catalogue. |
| `~/.bstep/assets/` | Atlas personnels prioritaires sur les fichiers fournis. |

Les ressources sont cherchées dans `~/.bstep`, à côté de l'exécutable, dans le dossier de données de l'installation, puis dans `/usr/share/bstep`. Pour ouvrir les exemples fournis, lance `bstep /usr/share/bstep/programmes`. Un nom relatif est d'abord résolu depuis le répertoire courant, puis dans les dossiers de ressources.


## Mode TUI

```sh
./bstep --tui
./bstep --tui --parcours parcours/02-deux-flux
make TUI_ONLY=1
./bstep
```

`--tui` lance bstep dans le terminal courant. Le binaire graphique y bascule aussi si l’affichage X11 ou l’ouverture de la fenêtre principale échoue et qu’un terminal interactif est disponible. `make TUI_ONLY=1` construit le même exécutable en mode terminal avec un compilateur C++20, sans dépendance à LibLapin, SFML ou X11. Un `make` ordinaire rétablit le lancement graphique par défaut.

Les panneaux se réorganisent au redimensionnement. Les terminaux larges affichent simultanément l’éditeur, la mémoire et les flux. Les terminaux plus étroits les présentent par onglets. Le clic active les commandes, le glisser sélectionne le code et la molette fait défiler la vue sous le pointeur. La souris fonctionne avec les émulateurs qui transmettent les événements xterm SGR, y compris à travers SSH.

| Touche | Action dans le TUI |
| --- | --- |
| F2 | Change de vue. En parcours, ouvre ou ferme la liste des exercices. |
| F3 | Bascule entre l’éditeur et les parcours. |
| F4 | Vérifie la réponse d’un exercice. |
| Maj+Tab | Passe d’un champ de réponse à l’autre. |
| F10 | Enregistre le fichier sous un autre nom. |
| Ctrl+Q | Quitte et rend la main au terminal. |

Les raccourcis de lecture F5 à F8, de débogage F9 et d’édition Ctrl+K et Ctrl+Y restent disponibles. Ctrl+C, Ctrl+X et Ctrl+V utilisent un presse-papiers interne dans le TUI. Pour coller depuis une autre application, utilise la commande de collage de ton terminal.

## Écrans

Le pseudo plein écran démarre avec une fenêtre par écran indépendant détecté par LibLapin. Les fenêtres prennent une taille commune qui tient sur tous les écrans. L’option --fenetre ouvre des fenêtres avec bordures. Le rendu et la souris utilisent les dimensions réelles de chaque fenêtre. Dans une fenêtre étroite, les panneaux intégrés sont accessibles par les onglets Éditeur, Mémoire et Terminal. L’explorateur et la console ont leurs propres onglets dans le panneau de débogage étroit.

| Écran | Vue |
| --- | --- |
| 1 | Éditeur et commandes d’exécution. La mémoire et le terminal y apparaissent quand leur fenêtre dédiée est absente. |
| 2 | Mémoire et pointeurs. |
| 3 | Compilation, stdin, stdout et stderr. La compilation prend plus de place avant le lancement, à la fin et lorsqu’une erreur arrête le programme. |
| 4 | Explorateur sur demande, console de débogage et variables surveillées. |
| 5 et suivants | Variables surveillées et points d’arrêt. |

La croix ferme une fenêtre secondaire et replace ses commandes dans l’éditeur au besoin. F10 rouvre les fenêtres secondaires. Le bouton Quitter et Ctrl+Q ferment l’application. La frise supérieure rappelle les raccourcis d’exécution, de débogage et de fichiers. Sur un seul écran, F9 ouvre la console de débogage dans la fenêtre principale.

## Fichiers

Au démarrage, l’éditeur ouvre un brouillon vide. Le menu Fichier permet de retrouver le brouillon, d’ouvrir un fichier, de l’enregistrer ou de l’enregistrer sous. L’explorateur parcourt les dossiers et sous-dossiers de la racine, présente les fichiers .c et .h et permet de créer des dossiers. Tu peux saisir un chemin relatif comme activites/exercice.c. Une deuxième validation confirme le remplacement d’un fichier existant dans Enregistrer sous.

- Ctrl+O ouvre l’explorateur.
- Ctrl+S enregistre le fichier actif ou demande un nom pour un brouillon.
- Ctrl+Maj+S demande un autre nom.

### Édition

- Tab réindente la ligne courante ou les lignes sélectionnées. Entrée indente la nouvelle ligne.
- Les accolades, les délimiteurs et les ponctuations de fin d’instruction réajustent l’indentation pendant la saisie. Les blocs utilisent quatre espaces et les accolades s’alignent sur l’instruction qui les ouvre.
- Ctrl+K coupe depuis le curseur jusqu’à la fin de la ligne. Si seuls des espaces ou des tabulations restent, il prend aussi le saut de ligne. Les coupes successives s’ajoutent au même texte.
- Ctrl+Y réinsère le texte ainsi coupé. Ctrl+Z annule une modification et Ctrl+Maj+Z la rétablit.

## Exécution et terminal

F8 exécute un pas, F7 restaure le pas précédent, F6 revient au début et F5 lance la lecture ou la met en pause. Après une modification, F8 ou F5 prépare et exécute la nouvelle version du fichier.

Saisis une ligne dans stdin et valide avec Entrée. Le moteur attend une entrée quand un appel en a besoin et la lecture automatique reprend après sa saisie. Le bouton EOF ou Ctrl+D dans ce champ termine le flux. La case « stdin et stdout réunis » affiche les saisies et la sortie standard dans l’ordre des événements. Le panneau stderr est accolé à stdout et possède son propre défilement à la molette. La boîte Compilation présente les diagnostics de l’interpréteur.

Les couleurs et le gras ANSI sont reconnus dans la sortie, par exemple avec printf("\033[1;36mTexte\033[0m\n"). Le mode joint conserve aussi les saisies lors du retour arrière. Une nouvelle saisie après un retour arrière crée une nouvelle suite d’exécution à partir du pas affiché.

## Surveillance et arrêts

Dans la console, Entrée exécute la commande saisie.

| Commande | Effet |
| --- | --- |
| watch main.x | Affiche la valeur courante de x et sa dernière valeur connue après sa sortie de portée. |
| watch repondre.mots[1][0] | Suit un élément indexé à l’intérieur d’un tableau de pointeurs. |
| break if repondre.parite == 1 | Interrompt la lecture quand la condition devient vraie. |
| list | Liste les règles et leurs numéros. |
| delete 3 | Retire la règle numéro 3. |
| next, back, continue | Avance, revient d’un pas ou reprend la lecture. |
| help | Affiche les commandes. |

Ouvre programmes/activites/entrees-et-fonctions.c depuis l’explorateur. Ajoute les règles ci-dessous, lance F5 et saisis 7 dans stdin. Reprends avec F5 après l’arrêt. Le caractère surveillé reste visible après le retour de repondre.

    watch repondre.symbole
    watch repondre.mots[1][0]
    break if repondre.parite == 1

## Parcours

Clique sur **Parcours** pour choisir une série d’exercices. Lis le code en lecture seule, saisis ta prédiction et clique sur **Vérifier** ou appuie sur Ctrl+Entrée. Entrée ajoute une ligne à la réponse. La simulation utilise le même interpréteur que le débogueur. Elle affiche uniquement le verdict, avec la possibilité de réessayer. La sortie, la mémoire et les commandes de débogage sont masquées dans ce mode.

**Exercices** ouvre la liste et montre les réussites. Sur deux écrans ou plus, la deuxième fenêtre accueille la réponse et le suivi. Sa fermeture ramène ces commandes dans la fenêtre principale. Le bouton **Éditeur** revient au travail en cours.

Les réussites sont enregistrées par compte Linux dans `$XDG_STATE_HOME/bdebug/parcours` ou `~/.local/state/bdebug/parcours`. À la réouverture, le parcours reprend au premier exercice à faire. La progression est associée au chemin du manifeste. Une modification du code, de la règle ou de l’entrée fixe entraîne une nouvelle validation de l’exercice.

### Les exercices fournis

Les six parcours principaux apparaissent dans **Parcours**, des sorties simples à la récursivité. Le premier propose neuf exercices, dont trois sur les boucles imbriquées. Les cinq autres en proposent six chacun. La banque complémentaire propose quatre variantes et deux défis pour chaque thème. Lance-la depuis le répertoire de ton choix pour choisir un exercice à retravailler.

    bstep --parcours parcours-complementaires

**Mise sous tension**, **Le passage de la porte** et **Les feux du quai** forment une première séance. Elles font intervenir l’affectation, le choix d’une branche et le suivi des tours de boucle. Les deux versions de la spirale, en flux puis préparée en mémoire, permettent de retrouver une même sortie à partir de deux ordres de calcul.

### Ajouter un parcours

Crée un sous-dossier dans `~/.bstep/parcours/` ou dans le dossier `parcours/` d'une copie de travail, avec un fichier `parcours.tsv` et les sources qu’il référence. Un dossier personnel remplace le parcours fourni sous le même nom. Le manifeste est en UTF-8. Sa première ligne utile donne le titre, puis chaque ligne donne un exercice dans l’ordre voulu. Les colonnes sont séparées par des tabulations. Les lignes vides et celles qui commencent par `#` sont ignorées.

```text
Lire des boucles
compteur.c	Compter les passages	sortie
conditions/test.c	Suivre la variable i	valeur:main.i
entree.c	Trouver le code de retour	retour	entree.txt
```

| Type | Réponse attendue |
| --- | --- |
| `sortie` | Le texte UTF-8 de stdout. |
| `stderr` | Le texte UTF-8 de stderr. |
| `flux` | Deux réponses, une pour stdout et une pour stderr. Le verdict porte sur les deux champs. |
| `retour` | L’entier renvoyé par `main`, en base dix. |
| `valeur:expression` | Un entier évalué à la fin de l’exécution. Par exemple, `valeur:main.tab[2]` vise une case du tableau. Pour un `char`, saisis sa valeur numérique. |

Pour les réponses de sortie, les espaces, les tabulations et les sauts de ligne comptent. Une ligne supplémentaire dans le champ représente un saut de ligne final. Si un flux est vide, laisse son champ vide.

La quatrième colonne est facultative. Elle désigne un fichier de stdin fixe, de 8 Kio maximum, transmis avant la simulation puis suivi d’EOF. Le bouton **Entrée fournie** donne accès à son contenu. Dans l’entrée et la réponse, un espace est représenté par `·` et une tabulation par `→`. Les chemins des sources et des entrées sont relatifs au dossier du manifeste. La vérification est limitée à 20 000 pas.

Le bouton **Actualiser** recherche les nouveaux parcours. Tu peux aussi ouvrir un dossier ou un manifeste directement.

    ./bstep --parcours parcours/lire-des-boucles
    ./bstep --parcours /chemin/parcours.tsv

### Poser une question pendant l’exécution

Place un commentaire `// ?` entre deux instructions d’un bloc, à l’endroit où les variables sont encore dans leur portée. Indique les expressions à évaluer, séparées par des virgules. Une ligne `// ? i, somme, notes[i]` demande trois valeurs entières, dans cet ordre. Dans le champ de réponse, sépare les entiers par des espaces.

Le parcours met le commentaire en évidence et attend la réponse avant de poursuivre. Pour un point qui revient dans une boucle, tu peux répondre passage par passage ou saisir plusieurs passages successifs du même point dans le champ, à raison d’une ligne par passage. Entrée ajoute une ligne et Ctrl+Entrée valide la saisie. En cas d’erreur, bstep conserve le point de départ et indique la ligne à corriger. Place le commentaire dans les accolades d’un `if` pour le limiter à une situation précise.

Après les points question, l’élève répond à la question finale définie dans le manifeste. La réussite est enregistrée à ce moment. Le débogueur interprète les marqueurs comme des commentaires C ordinaires.

## Langage interprété

Le moteur traite le fichier actif. Il prend en charge int, char, les blocs, if/else, while, for, return, les opérateurs arithmétiques et logiques usuels, les tableaux de taille littérale, les chaînes, les pointeurs et les fonctions simples définies dans ce fichier. Place les appels de fonctions définies dans le programme dans des instructions dédiées, des déclarations, des affectations ou des retours. La sortie accepte puts, putchar et printf. Pour viser un flux, passe stdout ou stderr directement à fprintf, fputs, fputc ou putc. Les formats de printf et fprintf sont %d, %c, %s, %p et %%. L’entrée accepte getchar et scanf avec %d, %c, %s et %%. Les directives d’inclusion reconnues sont stdio.h et stddef.h.

L’interpréteur affiche des adresses virtuelles et garde les 512 derniers états pour le retour arrière. Les règles de surveillance suivent les locaux par nom de fonction et peuvent traverser les tableaux et pointeurs du langage pris en charge.

## Sources du projet

- src/cmini.cpp contient l’analyse syntaxique et le moteur pas à pas.
- src/main.cpp relie l’interface LibLapin à l’exécution.
- src/desktop.hpp gère les fenêtres et src/source_directory.hpp l’explorateur.
- src/panels.hpp contient le rendu du terminal et les champs de saisie.
- src/course.cpp charge les parcours et valide les prédictions. src/course_view.cpp affiche les exercices et leur suivi.
