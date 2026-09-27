

======== overview ========



# Overview 
The probe has landed at the bottom of the ocean depths, and through the camera you can see hundreds upon thousands
    of pearls, scattered amongst the sea floor. Deep sea dragons live here too, so in order to blend in, you and your
    team will be controlling a group of magnificent leviathan-like automata. 
Each of your serpentine machines can collect pearls to grow in length; at the end of the mission, your longest robot
    (and its pearls) will be extracted back to the surface. 
As such, compete with other teams to stay alive and obtain the longest dragon!  
## Specifications 
Each team can control up to 64 dragons at a time. Your team will write exactly one program, which
    each dragon will execute, running its own copy of your program. A corollary of this is that
    individual dragons do not share any program memory with other dragons, and hence the only way to communicate
    dragon-to-dragon is via the sonar system. 
Each dragon gets approximately 25 milliseconds at most to compute and execute actions each turn.
    This is not entirely correct, so for more details about computational limits for each of your dragons, see the Timeouts section. 
## Gameplay 
Your resident games expert notices that the interactions between these sea dragons are similar to the rules of the
    retro video game Snake, save for some
    differences: 
- These dragons can spontaneously split in two. 
- Dragons only communicate through long-distance sonar. 
- A dragon can move multiple times within a turn, though at a cost. 
## Next Steps 
- You can read about the full rules of the game starting from Structure. 
- After that, feel free to started with your first bot at Quickstart! 
- Some more technical information about the engine and turn order starts from IO Protocol. 
- Once you've submitted your bot to the ladder, see ELO System to understand rating and matches. 
 NextQuickstart

======== quickstart ========



# Quickstart 
For beginners, we recommend using Python to write your bots. Make sure you have Python preinstalled on your machine. 
## Install the toolkit 
The toolkit, `unswbc`, is a Python package. If you have uv,
    install it as a tool. Upgrade later with `uv tool upgrade unswbc`. 

```
uv tool install unswbc
```

If you do not have uv, install it with pip instead. You need Python 3.11 or newer, and the command is then called
    the same way. Should pip refuse and say `externally-managed-environment`, as Debian, Ubuntu and Homebrew
    do, install uv and use the line above. 

```
pip install unswbc
```

## Check your machine 
Run `unswbc` on its own. It lists the commands and reports what it found here: a Python interpreter, the C
    and C++ compilers, and the replay viewer. Anything missing is named with the step that installs it. 

```
unswbc
```

## Create a bot 
`unswbc init` creates a bot project in Python, C or C++. A Python project consists of `main.py` (your bot), `helper.py` (the library that reads and writes the engine protocol) and `bot.toml` (the language and the list of files to ship). C and C++ projects contain `helper.h` and `helper.c`, or `helper.hpp`, in place of `helper.py`. Maps go into a `maps/` folder beside the bot folder, which all your bots share. 

```
unswbc init python mybot
```

After upgrading the toolkit later, `unswbc maps` adds any newly bundled maps to that folder. 
## Run a match 
`unswbc run` takes a map and two bot projects, rebuilds both bots, plays the match and writes a `.replay` file. The two bots may be the same project. `arena.map` is an 11×11 map to start on. 

```
unswbc run maps/arena.map mybot mybot
```

## Open the replay 
If you use VS Code, Cursor or VSCodium, `init` has already installed the replay viewer, so clicking the `.replay` file plays the match. Install it by hand if your editor arrived later. 

```
unswbc vscode
```

Otherwise open the visualiser and choose the file. Either way you see the whole board and every
    line your bots printed, neither of which a bot can see for itself. 
## Log in 
Make an API key on your team page — it starts with `bc_` — and hand it to the
    toolkit. `unswbc auth status` then names the team the key belongs to. 

```
unswbc auth set bc_...
```

## Submit 
`unswbc submit` zips the project and uploads it as a new version. `-n` names that version and `-d` records what changed. See Submitting via Website for what happens to the
    build afterwards. 

```
unswbc submit mybot
```

## Template Structure 

```
import helper as unswbc

ct: unswbc.Controller
game: unswbc.Game

def execute_turn() -> None:
    ct.make_move(unswbc.Direction.NORTH)

def main() -> None:
    global ct, game
    ct, game = unswbc.init()

    while unswbc.update(ct, game):
        execute_turn()
        unswbc.end_turn()

if __name__ == "__main__":
    main()
```

The template provides you with a game loop. `init()` reads the initial setup information from the engine,
    and `update()` will read in information from a new turn (or false, if the game ends or dragon dies). You
    should make changes to the `execute_turn()` function, and `end_turn()` will finish the bot's
    turn. Advanced teams can choose to write their own interface with the engine (see interface docs). 
The starter bot shuffles the four directions from a generator seeded the same way every run, then steps onto the
    first neighbouring tile that is not blocked by kelp or a dragon. All three starters do this, so a Python bot and a
    C++ bot behave alike. Take a look through the rest of the docs to improve this simple bot! 
## Next steps 
Be sure to test your bots on multiple maps! It may be useful to create your own maps to
    test how your bot reacts to specific situations or edge cases. The CLI page lists every command
    and its options. 
PreviousOverview NextSubmitting via Website

======== submitting ========



# Submitting via website 
Bots are submitted as a zip at Submissions, or with the cli command `unswbc submit`. One build at a time is active and plays against other teams. You must
    join a team before submitting. 
## Submission layout 
Upload your submission as a zip folder containing all the bot files. The config file `bot.toml` is required at the top level, see below for the contents for each language. The size limit is 4
    MB, and you may make 12 submissions per hour. You can change the active submission an unlimited number of times. 

```
[project]
language = "py"
include = [ "*.py" ]
```

## States 

 | State | Meaning
 | Processing | Building.
 | Ready | Built, not playing.
 | Active | Built and playing ranked matches.
 | Build failed | Did not compile, or the zip was formatted incorrectly. 
A version goes active as soon as it builds, replacing the one before it. Any Ready build can be activated, and
    earlier builds remain listed, so reverting is one click. 
## Build failures 
Confirm that `bot.toml` is at the top level of the zip, that the language on the form matches it, and
    that `include` lists every file, the helper included. Then unzip the archive into an empty directory and run a
    match from there; if that fails, the server's build fails for the same reason. If it still fails, ask on Discord and quote the version number. 
PreviousQuickstart NextStructure

======== structure ========



# Structure 
## Rounds and Turns 
Each game lasts at most 500 rounds. Within each round, every currently living dragon will perform
    some number of actions. One of these time steps is known as a turn. 
Turns are carried out in ascending order of dragon ID. This means on a given dragon's turn, all dragons with lesser
    IDs have already acted, and all dragons with greater IDs have not. For more details about the game loop, see Execution Order. 
## Scoring 
Each game has three possible outcomes: team A wins, team B wins, or a draw. 
Your team is eliminated when all of your dragons perish. Upon the elimination of any team, the game automatically
    ends, regardless of the current round number. If exactly one team is eliminated, the other wins. If both teams are eliminated in the same round, the game is a draw. Otherwise, after 500 rounds, the winner
    is decided by the following tiebreaks in order: 
- The team with the longest living dragon. 
- The team with the greatest total length across all of its dragons. 
- Draw (no winning team decided). 
PreviousSubmitting via Website NextGame Map

======== map-info ========



# Game Map 
## Dimensions 
Games will be played on maps which are two-dimensional rectangular grids. Both width and height will be between 10 and 64 cells inclusive. 
The top left corner will be denoted with coordinates (0, 0), with the x-coordinate increasing towards the right and
    the y-coordinate increasing down. North is oriented upwards. 
Dragons can query the dimensions of the map as follows: 

```
width, height = game.get_map_size()
```

## Symmetry 
To ensure fairness between teams, each map is guaranteed to be symmetric. This will either be via vertical
    mirroring, horizontal mirroring, or a 180 degree rotation. 
Dragons are not able to easily query the symmetry of the map; that one's up to you! 
## Topology 
The map edges 'wrap around'; specifically, a dragon that moves outside the bounds of the map will find itself on the
    other side. 
On any sort of visual representation of a map, this is denoted by dashed lines running down the east and south
    borders. Nothing renders on these borders (instead, see their equivalent west and north borders respectively). 

 R0 A dragon can keep moving right past the east border.  
## Composition 
Maps are composed of tiles and edges. Again, note that edges along the east and north map border
    are equivalent to their west and north counterparts (as dictated by the wrap-around). 
See Pearls for information about certain tiles, and Kelp and Portals for information about nonempty edge types. 
PreviousStructure NextPearls

======== pearls ========



# Pearls 
Pearls are the sole resource available, and are used to increase the length of dragons. 
## Pearl Spawning 
Each tile has a countdown which is visible to all dragons which can inspect the tile (see Vision). This countdown represents the number of rounds until said tile next attempts to
    spawn a pearl. Thus, each round the countdown decrements by 1. Countdowns are decremented at the start of each
    round, before any dragon moves (see Execution order). When the countdown reaches
    zero, the tile attempts to spawn a pearl. Regardless of the outcome of this attempt, the countdown resets. 
An attempt to spawn a pearl succeeds only when the tile is empty (i.e. no dragon segments nor pearls occupy it). 
Resetting the countdown involves drawing uniformly at random from [`min_gap`, `max_gap`].
    These values are tile-specific properties set on the map, but are not publicly visible to dragons. A tile with `max_gap` 0 never spawns pearls and always reports a countdown of -1. 

 R0 The marked tile's countdown reaches zero while it is empty, and so a pearl spawns.  

```
soonest = None
for tile in ct.get_tiles():
    if tile.get_pearl_time() < 0:
        continue  # never spawns
    if soonest is None or tile.get_pearl_time() < soonest.get_pearl_time():
        soonest = tile

if soonest is not None:
    where = soonest.get_position()
    ct.set_indicator_string(f"pearl at {where.x},{where.y} in {soonest.get_pearl_time()}")
```

## Symmetric Pearls 
On a symmetric map, each tile shares a countdown with its mirror image, so pearls will attempt to spawn
    symmetrically. Of course, if one of the tiles is blocked when the countdown reaches 0, a pearl will only spawn on
    the other tile. 
## Death 
When a dragon dies, it will drop half its length in pearls. See Death for more. 
PreviousGame Map NextKelp and Portals

======== kelp-and-portals ========



# Kelp and Portals 
Kelp and portals exist on the edges between tiles, not the tiles themselves. This includes the edges along the map
    border. 

 R0 This poor dragon travels through a portal, straight into kelp.  
## Kelp 
Kelp is the first type of special edge. Their job is to function as barriers; in particular, a dragon cannot pass
    from one tile to another across a kelp edge. If it tries, it dies (hit wall) and drops pearls like any other dead dragon. 
Kelp is a plant. There are no other special properties of kelp. 

```
def is_safe(direction: unswbc.Direction) -> bool:
    here = ct.get_position()
    tile = ct.get_tile(here)
    if tile is None:
        return False

    edge = tile.get_edge(direction)
    if edge.get_edge_type() == unswbc.EdgeType.KELP:
        return False

    ahead = ct.get_tile(here.add_dir(direction))
    return ahead is None or ahead.get_dragon() is None
```

## Portals 
Portals are the second type of special edge, and they allow for quick traversal of the map, conditional on your
    bravery. Each portal has an ID, which is an almost-unique non-negative integer. Exactly one other portal shares the
    same ID: these two portals are connected. 
There are a couple constraints on connected portals. A pair of connected portals are guaranteed to have the same
    orientation (i.e. both lying along a vertical edge or a horizontal edge). Furthermore, both portals either lie on
    the map's line of symmetry, or both lie off it. This ensures that each portal maps to exactly one other portal
    without ambiguity. 
Portals are double-sided, so you can enter from either side. Entering a portal from the left causes the head of your
    dragon to emerge from the right, and vice versa. The corresponding behaviour for vertically oriented portals is left
    as an exercise for you. 
Dragons cannot see through to the other side of portals (see Vision), so it usually won't
    know what's on the other side. 
Sonar travels through portals. 

```
edge = tile.get_edge(direction)
if edge.get_edge_type() == unswbc.EdgeType.PORTAL:
    ct.output_log(f"portal {edge.get_portal_id()} to the {direction.value}")
```

PreviousPearls NextVision

======== vision ========



# Vision 
Every dragon can only see the 7 by 7 square of tiles centred on its head; that is, all 49 tiles with
    a Chebyshev distance no greater than 3. This vision square wraps around the map as well due to its topology. 
Again, note that vision does not extend to the far side of a portal, which is visible only if it is within three
    tiles of the head by ordinary distance. 

 R0 A visualisation of a dragon's vision square as it moves. Note the interactions with portals and map borders.  
## Vision Information 
For each tile in view, the dragon receives its (absolute) coordinates, its contents (pearls and dragon parts), its
    pearl countdown (see Pearls) and its four edges (see Kelp and Portals). 

```
tile = ct.get_tile(pos)             # None when pos is outside the window

tile.get_position()                 # board coordinates, already wrapped
tile.get_pearl_time()               # rounds until a pearl spawns here; -1 if never
tile.has_pearl()                    # True if a pearl is on it now

edge = tile.get_edge(unswbc.Direction.NORTH)
edge.get_edge_type()                # EdgeType.EMPTY, EdgeType.KELP or EdgeType.PORTAL
edge.get_portal_id()                # id shared with the partner edge; -1 if not a portal

part = tile.get_dragon()            # None if no dragon is on it
if part is not None:
    part.get_team()                 # Team.A or Team.B
    part.get_id()                   # which dragon
    part.is_head()                  # True for the head
    part.get_dir()                  # head: its heading; body: the way to the head
```

A turn that steps onto a neighbouring pearl when it can see one, and onto a tile with no dragon on it otherwise: 

```
def execute_turn() -> None:
    here = ct.get_position()
    fallback = unswbc.Direction.NORTH

    for direction in unswbc.Direction.get_direction_list():
        tile = ct.get_tile(here.add_dir(direction))
        if tile is None:
            continue
        if tile.has_pearl():
            ct.make_move(direction)
            return
        if tile.get_dragon() is None:
            fallback = direction

    ct.make_move(fallback)
```

PreviousKelp and Portals NextMovement

======== movement ========



# Movement 
Movement is considered a type of action that a dragon can perform on a turn. Note that each dragon
    must output at least one action per turn (only the last one is actually performed; see Death for a list of death conditions). The only other type of action which can be performed is splitting. 
Movement, like all other unit actions, is performed only once the turn is complete. 
## Standard Movement 

 R0 Dragons move eastwards. 

Each dragon can move one tile in any cardinal direction (north, east, south, west) each turn. This causes the
        head to move in said direction, with the rest of the body following along. 
Dragons can even move backwards, though doing so causes its head to squeeze into its own neck, a move which
        often proves fatal. 
A dragon's facing direction is the direction of its head from its first body (non-head) segment.
        The facing direction of its body segments is defined similarly (direction to the next segment towards the head). 

```
ct.make_move(unswbc.Direction.EAST)
```

## Eating Pearls 

 R0 A dragon eats a pearl and grows longer by a unit. 

When a dragon moves its head onto a pearl, the pearl is automatically eaten and the tail does not advance that
        turn. In other words, the dragon grows a body segment. 
Eating pearls is the only way to gain length. 
## Collisions 

 R0 Everyone dies via various means except the orange dragon. 

A dragon dies if its head moves into any obstacles. This includes kelp, any segment of another dragon, or any segment of itself. 
Note that this death condition is checked before any actual movement. This means a dragon
        will die moving into own tail, even if the tail would have moved out of the way in the same turn. 
If the destination contains another dragon's head, both dragons die, regardless of their team. 

```
ahead = ct.get_tile(here.add_dir(direction))
part = ahead.get_dragon() if ahead is not None else None
if part is not None:
    if part.is_head():
        continue  # both of us would die
    continue      # only I would die
```

A dragon that dies drops pearls along its body. See Death for more information. 
## Sprinting 

 R0 Dragon decides to sprint north + north + east. 

Dragons may decide to move several times in one turn in varying directions. 
Although all moves happen within a single turn, collision checking (and pearl eating) is performed at every
        step. This means that in theory, a dragon might die performing `NNE`, but live with `ENN`,
        even though they target the same destination. 
Every additional move costs a unit segment of the sprinting dragon. More rigorously, moving x steps in
        a turn removes x - 1 segments. 

```
ct.make_moves([
    unswbc.Direction.NORTH,
    unswbc.Direction.NORTH,
    unswbc.Direction.EAST,
])
```

## Death by Sprint 

 R0 Dragon implodes due to oversprinting. 

A move with more steps than the dragon can pay for is not truncated. The dragon will perform the
        steps that can be paid for, and then will pay for the next step with its own life. 
PreviousVision NextSplitting

======== splitting ========



# Splitting 
Splitting is another action that dragons can perform. A dragon can only perform one action per turn. 

 R0 A length 6 dragon splits. The parent continues east. The child leaves west, head first, from the old tail. 

A dragon may split itself to create a new dragon on its team. Note that the minimum length for any dragon is 2.
        The child consists of the parent's rear segments in reverse order, so the parent's old tail becomes the child's
        head and the child faces away from the parent. 
The child receives the next unused dragon id, and takes its first turn later in the same round. The child is
        controlled by a brand new instance of your program, and has no memory of the parent. 
## Constraints 
A split is legal only if all of the following hold: 
- The child has length at least 2. `SPLIT 1` is never legal. 
- The parent has length at least 2 after the split. A dragon of length 6 may split 2, 3 or 4. 
- The team has fewer than 64 living dragons. 
Requesting an illegal split kills the parent with no valid action. 

```
if ct.can_split(2):
    ct.do_split(2)
else:
    ct.make_move(unswbc.Direction.NORTH)
```

PreviousMovement NextSonar

======== sonar ========



# Sonar 
## Sending 

 R0 Two teammates. Each turn the ray leaves the sender and stops at the first body it meets. 

Each turn a dragon may send up to four sonars, one in each direction: N, E, S and W. Each sonar is an unsigned
        64-bit integer. After the dragon's action has been applied, if the dragon is still alive, the sonars are cast in
        the order N, E, S, W. Each one travels in a straight line from the head. Sonars wraps around the board, passes
        through portals, and stops at the first kelp or dragon part it hits. If it hits a dragon, the message is
        delivered to that dragon, which may be the sender itself. If it reaches no dragon within `WIDTH + HEIGHT` tiles, it is lost. 

```
ct.send_sonar(Direction.EAST, 1806)
ct.send_sonar(Direction.NORTH, 2 ** 40)

for message in ct.get_sonar_messages():
    ct.output_log(f"heard {message}")

echoes = ct.get_sonar_echoes()
ct.output_log(f"enemy heads found: {echoes.enemy_head}")
```

## Edge cases 
Dragons have extremely high refractive indices, exhibiting Total Internal Refraction. Thus, sending a sonar into its
    own body will cause the sonar to exit at the tail, following the direction that the tail is pointing. Example: a
    east-ward facing dragon issuing a westward sonar will have its sonar emitted through its tail instead, directed at
    whichever direction the tail is facing. 
If you send two sonars in the same direction, only the last one is sent. 
## Receiving 

 R0 The first message reaches our friend, but then an enemy gets in the way and intercepts all our later messages. 

Sonars are read at the start of the recipient's next turn. A dragon with a higher id than the sender therefore
        receives the signal later in the same round, and a dragon with a lower id receives it in the following round.
        Several sonars may arrive in one turn, in the order they were sent. A signal is lost if the sender died during
        its move, since no signal is cast, or if it reached nothing. The replay shows the ray in either case. A signal
        may be received by a dragon on either team. No information about the sender or team is carried. 
## Echoes 
At the start of its next turn, the sender gets the counts for each kind of thing its sonars stopped on. A sonar that
    reached nothing adds no count. 

 | Kind | The sonar stopped because
 | `kelp` | it hit kelp
 | `ally` | it hit the body of a dragon on your team, your own included
 | `ally_head` | it hit the head of a dragon on your team
 | `enemy` | it hit the body of a dragon on the other team
 | `enemy_head` | it hit the head of a dragon on the other team 
As an example, if a dragon sent 4 sonar beams, 2 of them hit kelp, and one of them hit and ally's body, and the last
    sonar beam hit nothing, the counts will be 2, 1, 0, 0, 0 respectively. 
## A note on protocol 
This only works on protocol 3. Please ensure your helper is up to date. 
PreviousSplitting NextDeath

======== death ========



# Death 
A dragon dies when its head collides with something, or when its program fails to produce a valid action on its
    turn. 
## Causes of Death 

 | Reason | Cause
 | Hit Wall | Moved into kelp.
 | Hit Self | Moved into its own body, including its tail.
 | Hit Other Body | Moved into a segment of another dragon from any team.
 | Head to Head | Moved into another dragon's head, causing both dragons to die.
 | No Valid Action | Requested a sprint it could not pay for, requested an illegal split, produced a malformed command, or outputted no action. 
## Mandatory Action 
The engine treats a turn with no valid action as suicide. Any dragon whose program crashes or outputs nothing (or
    garbage) will be killed. Dragons which fail to end their turn before the time limit but have outputted at least one
    valid action will experience a different fate (see Timeouts). 
## Pearl Dropping 
Upon death, dragons exhibit a startling phenomenon: starting from their head, every second body segment crystallises
    into pearls. A dragon of length L therefore drops ⌈L/2⌉ pearls. 

 R0 A dragon dies and drops pearls at every second segment.  
PreviousSonar NextGame Format

======== game-format ========



# Game format 
By facing off with competitor teams through ranked and unranked games, you will get an ELO score that determines
    your spot on the leaderboard. Your ELO score increases when you win games, and decreases when you lose games. ELO System sets out the arithmetic and the tiers. 
## Ranked and unranked 
A ranked battle consists of five games and will cause team ELOs to update. Maps are chosen at
    random by the competition server. Ranked battles come from two places: the ones you request, and the autoscrim draws
    below, which are ranked as well. In an unranked battle, teams may choose to verse another team on particular
    maps; one game is played for each map. Unranked battles do not update ELOs nor rankings. Your team keeps the same colour
    across every game of a battle, and after each battle you will be able to see the replays. 
## Requesting a battle ('scrim') 
You can request a battle against a specific team on the leaderboard or from a team's page
    in find a team. The battle outcome appears in Battles, which the My team box narrows to your own. Note that all battles are publicly viewable, so be wise
    about concealing your strategy! 
Both teams need an active submission. Unranked battles are then open to every team; ranked ones need the switch on
    under Settings on both Team pages, and must target a team rated no more than
    50 below you, although anyone above you is fair game. That switch waits eight hours between flips, so
    nobody can duck a rating by turning it off between requests. Your team may start 60 games an hour by hand, which is 12
    ranked scrims. Battles against organising teams (marked DEV) have a separate allowance of another 60 games an hour. 
## Battles you are drawn into ('autoscrim') 
Autoscrim battles are ranked. They move your rating exactly as a ranked battle you asked for does, and
    most of the games your team plays will be these, so your place on the ladder is mostly decided by them. 
You do not have to ask for every battle. Every two hours, on the UTC boundary, the ladder is sorted and each team is
    drawn up to two distinct opponents from the eight places above and the eight below it, so you meet teams near your
    own rating. The draw favours opponents you have not played for a while, and over a day you see most of the ladder
    around you. 
You are in the draw whenever you have an active submission; the ranked switch in your settings governs only the
    battles teams request. Each draw is a ranked five-game series on five maps chosen at random, at most two per team
    per two-hour batch, and it does not spend your hourly allowance. You may receive fewer if no suitable opponents
    remain. 
## Submitting a bot 
Using either the CLI or the website, you may upload and submit code. Once submissions are made, they cannot be
    deleted. Only one submission can be 'active' at a time. Your 'active' submission is the one that the game server
    will use for any incoming / outgoing battles. 
Your rating belongs to the team and carries over when you switch submissions, but a new submission moves it faster.
    For its first 10 ranked battles, wins and losses count for more, so a better bot climbs quickly; after
    that it settles and single results only nudge it. Only one new bot per team in 12 hours gets this, and uploading the same
    code again doesn't. See New bots. 
PreviousDeath NextELO System

======== elo ========



# ELO System 
There is one ladder and one rating per team, starting at 1500. Every ranked battle moves both teams along it;
    unranked battles leave it alone. 
## What a rating means 
A rating is a prediction. Put two of them side by side and Elo turns the gap into an expected share of the battle's
    five games. Two equal teams are predicted to split them 2.5 each. A team 400 points ahead is predicted to take about
    4.5 of the 5. 
When the battle finishes, what actually happened is compared with that prediction. If you outperform relative to
    your ELO, you gain rating and the other team loses it; if you fall short, you lose ELO. 
This is why a rating eventually settles somewhere. If you sit above the level you actually play at, you will keep
    losing ELO until your results support where you are. 
## Why a win can cost you rating 
Your score in a battle is your proportion of games won, not whether you won overall. A ranked battle is five games,
    so a 3–2 win counts as 0.6, not 1. 
Against a team 400 points below you, the prediction is about 4.5 games of the 5. Win 3–2 and you took 3, well short
    of what your rating claimed you would do, so rating moves to them even though you won the battle. At that gap even
    4–1 costs you, and only a 5–0 sweep gains anything. 
The same rule is what makes playing upwards worth it. An underdog predicted to take half a game is already ahead of
    the prediction with one win, so a 1–4 loss gains rating. 
## Examples 

 | Your rating | Opponent | Predicted | Result | Change | New bot
 | 1500 | 1500 | 2.5 of 5 | 3–2 | +2 | +10
 | 1500 | 1500 | 2.5 of 5 | 5–0 | +12 | +48
 | 1900 | 1500 | 4.5 of 5 | 3–2 | -7 | -30
 | 1500 | 1900 | 0.5 of 5 | 2–3 | +7 | +30 
Equal teams, won 3–2. The prediction is an even 2.5 games each, and you took
    half a game more than that, so +2 changes hands. A close battle between equals barely moves either
    of you. 
Equal teams, won 5–0. The same prediction, but there was nothing left to take. That is as far past
    an even prediction as a result can get, so you collect +12, the biggest swing two equal settled
    bots can produce. A new bot would collect +48. 
400 points ahead, won 3–2. Your rating predicted 4.5 games of the 5,
    and 3 is well under it. You won the battle and still paid 7 rating for it. The favourite is
    not being punished for winning; it is being charged for winning by less than it claimed it would. 
400 points behind, lost 2–3. The inverse of the row above. You were predicted to take
    0.5 games, took 2, and +7 comes to you despite losing the battle. 
The Change column is for a bot that has settled. A new bot moves further on the same result, shown in the last
    column; see below. 
## New bots 
Each submission has its own K, set by how many ranked battles it counts as having played. At 0 it is K =
    96, so a new bot can climb or drop to where it belongs in a handful of battles. K then falls evenly to K =
    24 at 10 and stays there, so once a bot has found its level, a lucky or unlucky draw of maps only
    nudges it. 
A bot's count is its starting count plus the ranked battles it has played itself. The starting count is fixed when
    the bot plays its first ranked battle, by the first of these that applies: 
- Same code. If your team has already played ranked with a bot of exactly the same code, the new one
        starts from that bot's count (the highest, if there are several). A re-upload never starts over. 
- Second new bot within 12 hours. If another of your team's bots started from 0 in the
        last 12 hours, the new one starts from the count of the bot your team last played a ranked battle with,
        capped at 10. 
- Otherwise it starts from 0 at K = 96, and your team's 12 hours start again. 
Same code means the same files with the same contents. How you zip them does not matter: timestamps, compression, a
    single folder wrapping everything, and `__MACOSX`, `.DS_Store` and `__pycache__` entries are ignored. Changing any file, even a comment, makes it different code, and rule 2 then decides. 
Activating an older submission again carries on from its own count. One that has never played ranked is decided by
    the rules above when it first does. Bots that played ranked before these rules came in keep counting their own
    battles from 0, and bots uploaded before them are never matched as the same code. 
For example, v1 plays its first ranked battle at 9:00, starting from 0, and has played 4 by 13:00. At 13:00 you
    upload v2, which is different code, within 12 hours of v1's start, so it starts from 4 (K ≈ 67). At 14:00 you upload v1 again: same code as v1, so it starts from v1's 4 plus nothing new, still 4. From 21:00, 12
    hours after v1 started, the next bot with new code starts from 0 again. 
Both teams in a battle use their own K. A new bot beating a settled one gains a lot, and the settled team loses only
    what its own K allows. 
## The formula 
Ratings are updated by Elo, applied once per battle rather than per game. Against a team rated RB, a team rated RA has expected share 1 / (1 + 10(RB − RA) / 400). Its actual share is the fraction of the battle's games it won, with a draw counting as half. Each team's rating
    changes by its own K times the difference, rounded. K is 96 at a count of 0 (see New bots) and falls by 7.2 with each battle, down to
    24 from 10 on. 
A battle is settled only when all of its games have finished, so a five-game series changes the rating once.
    Unranked battles do not affect ratings. The autoscrim draw is ranked, so those battles move your rating too, and
    they are most of what you play. 
## Tiers 

 | Tier | Rating
 | Fishing Boat | 2800+
 | Leviathan | 2500–2799
 | Orca | 2200–2499
 | Shark | 1900–2199
 | Swordfish | 1600–1899
 | Tunafish | 1300–1599
 | Shrimp | 1000–1299
 | Plankton | 0–999 
PreviousGame Format NextCLI

======== cli ========



# The CLI 
`unswbc` creates a bot project, builds it, plays matches and uploads the result. The commands below are
    in the order you will need them. `unswbc help <command>` lists the options for any one of them. 
## Recommended 
`unswbc` is a Python package, and it carries the judge's clang, so C and C++ bots build in the sandbox
    with no further installing. Install uv if you do not have it, open a new
    shell so it is on your PATH, then install the toolkit. Later, `uv tool install unswbc@latest` takes the newest version and `uv tool install unswbc==0.3.0` pins one. The toolkit carries the engine, so an old one plays by different rules than the judge: it asks PyPI once a day
    and offers to install a newer one when there is one. `UNSWBC_NO_UPDATE=1` silences that. 

```
uv tool install unswbc
```

## If you don't have uv 
pip installs it just as well, given Python 3.11 or newer, and puts `unswbc` on your PATH. Debian, Ubuntu
    and Homebrew refuse a plain pip install and say `externally-managed-environment`; on those, uv is the
    way. 

```
pip install unswbc
```

## unswbc 
On its own it lists the commands and checks this machine: the Python interpreter, the C and C++ compilers, VS Code
    and the replay viewer. Anything missing is named with the step that installs it. 

```
unswbc
```

## unswbc init 
Creates a project in `c`, `cpp` or `python`, holding a starter bot and the helper
    library. The directory defaults to the current one. Maps do not go inside it: they land in a `maps/` folder beside the project, which every bot of yours can share. 

```
unswbc init python mybot
```

## unswbc update 
Replaces a project's helper files with the ones this toolkit ships, so run it after upgrading the toolkit. Your own
    files are not changed, and each old helper is kept as a `.bak` file next to it. It also writes every
    bundled map into the `maps/` folder beside the project, replacing the ones that changed. The directory
    defaults to the current one. See Protocol upgrade. 

```
unswbc update mybot
```

## unswbc maps 
Adds any bundled map the folder is missing, and leaves your own files alone, so run it after upgrading the toolkit.
    The folder defaults to `maps`; name another to choose it, such as `unswbc maps ../maps` for a project
    you made in the current directory. 

```
unswbc maps
```

## unswbc run 
Plays one match between two bots on a map and writes a replay. Each bot is a project directory, rebuilt before it
    plays, and naming the same one twice plays it against itself. Add `-v` to watch every round, or `--sandbox` to run a Python, C, C++ or wasm bot the way the judge does: priced in CPU points, with its logs,
    indicators and drawings capped. Without it, a local match keeps all of them. 

```
unswbc run maps/arena.map mybot mybot
```

## unswbc vscode 
Installs the replay viewer into VS Code, Cursor or VSCodium, so any `.replay` file opens in the editor. `init` already does this when it finds one. 

```
unswbc vscode
```

## unswbc auth 
Stores the API key from your team page, so this machine may submit. `unswbc auth status` shows the key in use, and `unswbc auth clear` removes it. 

```
unswbc auth set bc_...
```

## unswbc submit 
Zips the project and uploads it as a new submission. `-n` names the version and `-d` records
    what changed. See Submitting via website. 

```
unswbc submit mybot
```

## unswbc log 
Prints the errors the toolkit has hit on this machine. Send it with any bug report; API keys are masked. 

```
unswbc log
```

PreviousELO System NextExecution Order

======== execution-order ========



# Execution order 
This page gives the engine loop in the order it runs. Most surprising replays come down to two facts: dragons act
    one at a time in id order, and a multi-step move is a sequence of separate moves. 
## The round 
- Pearls tick. Every tile's countdown decreases by one, top row to bottom, left to right. At zero
        the tile spawns a pearl if it is free, and draws a new countdown either way. See Pearl spawns. 
- Each living dragon takes a turn, in ascending id order. A dragon that died earlier this round
        is skipped. A dragon created by a split this round is appended to the list and takes its turn later this round. 
- The round ends. The game ends if a team has no dragons, or if this was round 500. 
## One turn 
- The dragon's sonar inbox and echo counts are written to the bot's input and emptied. 
- Defaults are set: action = suicide, sonars = none, indicator = none. 
- The bot's output is read line by line until `ENDTURN`. 
- The action is applied. 
- Sonars are cast in the order N, E, S, W, if the dragon is alive. 

```
LOG heading for the pearl at 4,7
DOT 4 7 0 255 0
MOVE NNE
SONAR E 90210
PROTOCOL 3
ENDTURN
```

### Commands 
`MOVE`, `SPLIT` and `INDICATOR` each overwrite the previous value, and `SONAR` overwrites the previous one in the same direction. The last one read takes effect, and none is
    applied until reading ends. `LOG`, `DOT` and `LINE` are applied immediately, in the order printed. A line that cannot be parsed is skipped, with an engine
    log entry, and does not change the action already set. 

```
MOVE N
MOVE E
ENDTURN
```

That pair moves east. 
## Applying a move 

 R0 Three steps east asked for. The third is blocked, so the dragon dies after two. 

For each step, in order: 
- If this is not the first step, check that the dragon can pay for it. A dragon of length 2 cannot, and dies
            with no valid action. 
- The dragon's facing is set to the step's direction. The destination is the tile across the edge now faced:
            kelp kills the dragon, a portal leads out of its partner edge, and otherwise the destination is the adjacent
            tile, wrapped. 
- The destination is checked against the dragon's own body first, then against other dragons.
            Another dragon's head there dies first, then this dragon. Any other segment kills only this dragon. 
- The head moves. A pearl on the destination is eaten. The tail advances unless a pearl was eaten. If this is
            not the first step, one further tail segment is removed. 
A death on any step discards the remaining steps and the turn's sonar. Steps already taken stand. 
### The dragon's own tail 

 R0 Coiling inward until the only tile left is its own tail, which has not moved yet. 

The self-collision check in step 3 includes the tail and runs before the tail advances, so the tail's tile
        is blocked for that step. Within a multi-move, the second step is checked against the body as the first step left
        it, so a tile that is fatal as a first step may be safe as a second. 
## Applying a split 
The conditions are checked before anything moves: child length at least 2, parent length at least 2, team below `UNIT_LIMIT`. Failing any of them kills the parent with no valid action. The child is the rear
    segments in reverse, appended to the live list with the next id, and takes its own turn later this round from a new
    process. 
## Casting sonar 
After the action, if the dragon survived, one ray for each direction set, in the order N, E, S, W. Each ray leaves
    the head. A ray opposite to a dragon's direction instead leaves the tail, in the direction the tail points away from
    the body. Each ray goes through portals, around the wrap, and stops at kelp or at the first living dragon (including
    the sender's own body). The value is placed in that dragon's inbox and read at the start of its next turn, so a
    higher id receives it this round and a lower id next round. The sender gets one echo count for what each ray stopped
    on. 
## The death sequence 
- The death is recorded with its reason. 
- Every second segment of the body as it stands becomes a pearl: head, skip one, next. Pearl countdowns are
        not changed. 
- The dragon is removed from the board. 
- Its remaining steps and its sonars are discarded. 
In a head-on collision, the other dragon's sequence runs first, then this dragon's. 
## When a turn ends 
A turn ends at the first of: the bot printing `ENDTURN`, the bot blocking to read its next turn, the bot
    exiting, or the bot reaching its limit (see Timeouts). In the first two cases the
    turn's output is delivered to the engine. In the last two nothing is delivered, the action stays at its default of
    suicide, and the bot is restarted as a new process with a fresh init block. 
PreviousCLI NextTimeouts

======== timeouts ========



# Timeouts 
## Local runs 
`unswbc run` runs bots as ordinary processes on your machine and places no limit on how much work a turn does.
    The only limit is a wall clock of 10 seconds per turn. A bot that reaches it, or that exits during a turn, delivers nothing
    for that turn, so its dragon dies, and the bot is restarted as a new process for the dragons it still controls. As a result,
    a local match says nothing about whether a bot fits within the judge's budget below. 
`unswbc run --sandbox` runs bots in the judge's sandbox instead, priced in CPU points exactly as the
    judge prices them. Python bots run on the judge's own interpreter, C and C++ sources are compiled to wasm by the
    judge's own clang with the judge's flags, and a bot already compiled to wasm runs there too: name a directory
    holding `main.wasm`, or the file itself. With `-v` it prints the points and memory each turn used. 
## Judge limits 
The judge runs bots in a WebAssembly sandbox and measures work in CPU points rather than time. Each instruction has
    a fixed price, so load on the judge cannot affect results. 

 | Limit | Value
 | Points per dragon per turn | 100 million
 | Memory per dragon | 48 MB
 | Threads per dragon | 1 (`thread_spawn` is denied)
 | Backstop | 1 s of CPU, 10 s wall; only catches a bot that evades the meter 
A turn that exhausts its points is stopped, delivers no reply, and the dragon dies that turn, so leave a margin. The
    first turn is not exempt: the interpreter and NumPy are preloaded, but your own imports and setup are charged.
    Spread heavy precomputation across turns or perform it lazily. 
A turn's budget runs from reading the round to the flush in `end_turn()`, and that is the figure `-v` reports. Work done after `end_turn()`, before the next round is read, is charged to the
    turn that follows it, so precomputing for the next turn there costs the same as doing it at the top of that turn. 
## Instruction prices 
This is the whole price list. Anything not named in it costs 2 points. 

 | Instruction | Points
 | Constants, `local.get`/`set`/`tee`, `global.get`/`set`, `select`, `nop`, `drop`, `block`, `loop`, `end`, `else`, `unreachable` | 1
 | Comparisons, integer and float | 1
 | Integer and float arithmetic: add, subtract, multiply, bitwise, shifts, rotates, `clz`, `ctz`, `popcnt`, `abs`, `neg`, `ceil`, `floor`, `trunc`, `nearest`, `sqrt`, `min`, `max`, `copysign` | 1
 | Conversions: wrap, extend, truncate, convert, promote, demote, reinterpret | 1
 | `ref.null`, `ref.is_null`, `ref.func` | 1
 | Loads and stores, of every width | 2
 | `memory.size` | 2
 | `br`, `br_if`, `if`, `return` | 2
 | Everything else, including every 128-bit SIMD instruction | 2
 | Division and remainder, integer and float | 3
 | `br_table` | 3
 | `call` | 4
 | `call_indirect`, `call_ref`, `return_call`, `return_call_indirect`, `return_call_ref` | 6
 | `memory.copy`, `memory.fill`, `memory.init` | 10, plus 1 per 8 bytes
 | `table.get`/`set`/`size`/`copy`/`fill`/`init`, `data.drop`, `elem.drop` | 10
 | `memory.grow`, `table.grow` | 50 
Work the judge does for you is charged to the same meter. 

 | Call | Points
 | A write to stdout or stderr | 2,500,000, plus 4,000 per byte
 | Reading from stdin or a file | 6 per byte
 | Drawing random bytes | 40,000, plus 3 per byte
 | Opening a file or looking a path up | 40,000 
For reference, parsing one round in the Python helper costs about 10 million points, and a flood fill of a 32×32 map
    costs about 19 million in Python and about 0.3 million in C++. 
## Output 
Each write costs 2.5 million points plus 4,000 per byte, and stderr is charged like stdout. The helpers flush once
    per turn, in `end_turn()`, and reading the next turn also flushes. A bot that flushes after every line
    pays 2.5 million points per line, so keep logging light and do not add flushes. 
The judge's stdout looks like a terminal (`isatty` returns true), so it is line buffered until something
    asks otherwise, and every line you print is then its own write. The C and C++ helpers ask otherwise: they call `setvbuf` for a full buffer in `init`, so a whole turn's output leaves in the one write that `end_turn` flushes. Ten log lines cost 2.5 million points that way, against 25 million a line at a time. 
So in C and C++, do not set `std::unitbuf`, do not call `sync_with_stdio(false)`, which gives `std::cout` a buffer of its own outside the one the helper set, and do not call `cin.tie(nullptr)` unless you flush at `ENDTURN` yourself. Use `std::clog` rather
    than `std::cerr` for debugging output. Python needs none of this: its stdout is fully buffered already. 
The judge keeps up to 500,000 log, indicator and drawing lines per team per game, and up to 16 MB of their text.
    Past either, the rest of that team's output is dropped and the replay says so once. An indicator is cut to 512
    characters. `unswbc run` keeps everything unless you pass `--sandbox`. 
## Languages 
C and C++ are compiled with `-O2 -msimd128` against the C standard library and libc++, with the zip's own
    directory on the include path. `__DATE__`, `__TIME__` and `__TIMESTAMP__` are
    errors, so a bot is the same whenever it is built. No other libraries are available, so any you need must be
    vendored as source. A SIMD instruction costs 2 points and executes as real SIMD, so 4 or 16 lanes of work cost about
    what two scalar instructions do. Python is CPython 3.13 with NumPy 2.5 and nothing else; pure-Python dependencies
    must be vendored. The sandbox drops the parts of both standard libraries that need a network, a second thread or a
    writable disk, which Standard library lists. 
## Determinism 
Games are reproducible. Random numbers come from a generator seeded per dragon and per game, and the clock advances
    with points spent, so `time.sleep` costs no real time and timing your own code measures points. 
Each game's seed decides when pearls respawn and what every dragon draws, and its replay shows it. Running `unswbc run map a b --sandbox --seed <seed>` with the same two bots plays the same game on your
    machine. A dragon only gets its own numbers if it asks the sandbox for them. C's `rand()` without `srand` and a default-constructed `std::mt19937` start from a fixed seed, and `srand(time(NULL))` reads a clock that starts at the same moment for every dragon, so all three give every
    dragon the same sequence. 
## The budget is per turn, not per tile 
`MOVE NNE` covers three tiles with one decision, so a deeper search can commit to several steps at once, paying
    in segments rather than points. 
PreviousExecution Order NextStandard Library

======== libraries ========



# Standard library 
The judge runs your bot in a WebAssembly sandbox with no network, no child processes and no writable disk. Modules
    that only exist to reach one of those are not shipped at all, so they fail at `import` rather than at the
    call. Everything else is present. Timeouts covers what the sandbox charges you for. 
## Versions 
Two things run your bot: the judge, and your own machine when you `unswbc run`. The judge is the one that
    scores you. 

 | Language | In the judge | On your machine
 | Python | CPython 3.13, with NumPy 2.5.3 | the first of `python3`, `python`, `py -3` that runs
 | C | Clang 20 as C17, against wasi-libc | the first of `cc`, `gcc`, `clang`, `cl`
 | C++ | Clang 20 as C++20, against libc++ | the first of `c++`, `g++`, `clang++`, `cl` 
Both sides compile at `-std=c17` or `-std=c++20`, and `-O2`. The judge adds `-msimd128`, puts your zip's own directory on the include path, and fails a build that uses `__DATE__`, `__TIME__` or `__TIMESTAMP__`. Set `UNSWBC_PYTHON`, `CC` or `CXX` to choose the tool a local run uses, and `unswbc` on its own reports what
    it found. The toolkit itself needs Python 3.11 or newer, whichever language your bot is in. 
A local run uses your interpreter, not the judge's, so a bot that relies on something added in 3.13 can pass in the
    judge and fail on your machine. `bot.toml` names the language as `py`, `c` or `c++`, and that is what the judge builds it as. 
## Python 
CPython 3.13 and NumPy 2.5.3, and nothing else. Third-party packages cannot be installed; a pure-Python dependency
    has to be vendored into your zip. These modules import: 

```
__future__  abc  argparse  array  ast  atexit  base64  bdb  binascii  bisect  builtins
calendar  cmath  cmd  code  codecs  codeop  collections  colorsys  compileall  configparser  contextlib
contextvars  copy  copyreg  cProfile  csv  ctypes  dataclasses  datetime  decimal  difflib  dis  doctest
encodings  enum  errno  faulthandler  fcntl  filecmp  fileinput  fnmatch  fractions  functools  gc  getopt
getpass  gettext  glob  graphlib  hashlib  heapq  hmac  importlib  inspect  io  itertools  json  keyword
linecache  locale  logging  marshal  math  mmap  modulefinder  netrc  numbers  opcode  operator  optparse  os
pathlib  pdb  pickle  pickletools  pkgutil  platform  posix  posixpath  pprint  profile  pstats  pty  pwd
py_compile  pyclbr  queue  quopri  random  re  reprlib  rlcompleter  runpy  sched  secrets  select
selectors  shlex  shutil  signal  site  stat  statistics  string  stringprep  struct  symtable  sys  sysconfig
tabnanny  tarfile  tempfile  termios  textwrap  time  timeit  token  tokenize  tomllib  trace  traceback
tracemalloc  tty  types  typing  unicodedata  unittest  uuid  warnings  wave  weakref  zipapp  zipfile
zipimport  zoneinfo
```

Names starting with an underscore are omitted above but behave as they do in any CPython: `_thread`, `_pyrepl`, `_collections_abc` and the rest are all there. 
### Not shipped 

 | Reason | Modules
 | No network | `socket`, `socketserver`, `ssl`, `http`, `urllib`, `xmlrpc`, `ftplib`, `smtplib`, `poplib`, `imaplib`, `telnetlib`, `wsgiref`, `ipaddress`, `email`, `cgi`, `cgitb`, `webbrowser`
 | One thread, one process | `asyncio`, `multiprocessing`, `concurrent.futures`
 | Compression libraries not built | `zlib`, `bz2`, `lzma`, `gzip`
 | Parsers and stores not built | `xml` (all of it), `sqlite3`, `dbm`, `plistlib`, `mimetypes`, `mailbox`, `importlib.metadata`
 | No terminal, no display | `curses`, `readline`, `tkinter`, `turtle`
 | Packaging and test tooling | `ensurepip`, `venv`, `distutils`, `lib2to3`, `test`, `idlelib` 
### Present, but will not work 
These ship, and cannot do the thing you would reach for them for. 

 | Module | What happens
 | `threading` | `Thread.start()` raises `RuntimeError: can't start new thread`. The lock and event
                types work, and a timed `acquire` returns at once, since the clock is virtual.
 | `subprocess` | `Popen` raises `OSError` 58, `ENOTSUP`. So does `os.fork`.
 | `shelve` | `open` fails, because `dbm` is not shipped.
 | `pydoc` | Will not import at all: it reaches for `urllib` on the way in. 
`zipfile` and `tarfile` import, but without `zlib` only stored members read; anything
    deflated, gzipped or xz'd raises. 
## C and C++ 
Clang 20 against wasi-libc and libc++, as C17 or C++20. Every libc++ header is present except `<generator>`, and so is every POSIX header the sysroot carries, including `<sys/socket.h>`, `<pthread.h>` and `<spawn.h>`. Those compile
    and link; the calls behind them fail at runtime. `socket()` returns `ENOTSUP`, `std::thread` throws `Resource temporarily unavailable`, and `std::system` returns −1.
    There are no third-party libraries, so anything you need must be vendored as source. 
## What both languages see 

 | Thing | In the sandbox
 | Network | Every socket call returns `ENOTSUP`.
 | Processes and threads | `fork`, `exec`, `posix_spawn` and thread creation are all denied.
 | Filesystem | Read-only. You get your own files at `/bot`, which is the working directory, and the
                interpreter or sysroot. There is no writable `/tmp`.
 | Environment | Five `PYTHON*` variables and `TERM=dumb`. `getpid()` is 1 and `os.cpu_count()` is 1. 
## Time 
Nothing about time is disabled, but the clock is the judge's, not the machine's. It reads 2026-01-01T00:00:00Z when
    your dragon has spent nothing, and advances one nanosecond per CPU point spent, plus whatever you asked to sleep. 
That makes `time.perf_counter` and `std::chrono::steady_clock` budget meters: a turn's 100 million
    points read as 0.1 seconds, so the difference across a piece of your code is the points it cost, in nanoseconds. A sleep
    returns immediately and moves the clock by what you asked for, which is why timing across one is meaningless. Two runs
    of the same match give the same readings. 
## Randomness 
Also the judge's. `os.urandom`, `random`, `std::random_device` and `std::mt19937` seeded from it all draw on one stream per dragon per game, so a replayed match draws the
    same numbers. `std::random_device::entropy()` reports 0, which is honest. `PYTHONHASHSEED` is
    0, so `hash()` and set and dict ordering are stable too. Seed your own generator if you want your dragons to
    differ from each other. 
PreviousTimeouts NextHelper Reference

======== helper ========



# Helper reference 
Everything the helper library gives you, taken from the `helper.py` that `unswbc init` put
    in your project. The helper is yours: you may edit it, or replace it with your own reader of the wire protocol. 
This page follows the helper the current toolkit ships. A project made with an earlier one keeps the copy it was
    given, so if a call listed here is missing from yours, upgrade the toolkit and take the helper from a fresh `unswbc init`. The three languages offer the same calls under each language's own names. 

## Constants 
What the engine is fixed at: 500 rounds, a 7x7 window, length 3 at spawn, length 2 minimum. 
## Direction 
One of the four compass directions.
North is up, and y grows downwards. 

`value() -> str` 
The protocol letter, N, E, S or W.
`get_direction_list() -> list[Direction]` 
A fresh list of all four, north, east, south, west.
`get_offset() -> tuple[int, int]` 
The (dx, dy) of one step this way, with y growing downwards.
`get_opposite() -> Direction` 
The reverse direction.
`get_left() -> Direction` 
A quarter turn anticlockwise.
`get_right() -> Direction` 
A quarter turn clockwise.
## Team 
Which side a dragon plays for. 

`get_enemy_team() -> Team` 
The other team.
## Position 
A tile's x and y, with (0, 0) at the top left and y growing downwards. 

`add_dir(direction: Direction) -> Position` 
The position one step that way, wrapped around the map edges.
`is_in_map()` 
Whether these coordinates are on the board, which only matters for ones you worked out yourself.
`is_in_vision()` 
Whether this tile is inside your 7x7 window this turn.
## Game 
The board and the round number, which every dragon sees the same way. 

`get_round_num() -> int` 
The round being played; the game ends after 500 of them.
`get_map_size() -> tuple[int, int]` 
The map's (width, height) in tiles.
`get_unit_limit() -> int` 
The most dragons one team may have alive at once, which is what caps splitting.
## Vision 
The 7x7 block of tiles around your head, parsed as you ask for it. 

`get_tiles() -> list[Tile]` 
All 49 tiles, row by row from the top left.
`get_tile(pos: Position) -> Tile | None` 
The tile at pos, or None if pos is outside the window.
## Tile 
One square of the board, with whatever stands on it and its four edges. 

`edges() -> dict[Direction, Edge]` 
The four edges keyed by direction.
`get_edge(direction: Direction) -> Edge` 
The edge you would cross stepping that way.
`get_dragon() -> DragonPart | None` 
The dragon segment standing here, or None if the tile is clear.
`has_pearl() -> bool` 
Whether a pearl is on this tile right now.
`get_pearl_time() -> int` 
Rounds until this tile next tries to spawn a pearl, or -1 if it never does.
`get_position() -> Position` 
This tile's position on the board.
## DragonPart 
One segment of a dragon, head or body. 

`get_position() -> Position` 
Where this segment is.
`get_id() -> int` 
The dragon this segment belongs to.
`get_team() -> Team` 
The team that dragon plays for.
`get_dir() -> Direction` 
The direction this segment faces.
`is_head() -> bool` 
Whether this is the head; stepping into another dragon's head kills both of you.
## EdgeType 
What lies between two tiles: EMPTY, KELP or PORTAL. 
## Edge 
The boundary between two tiles. 

`is_passable() -> bool` 
Whether a dragon can cross this edge; kelp is the only thing that stops one.
`is_portal() -> bool` 
Whether crossing this edge comes out at its partner edge.
`get_edge_type() -> EdgeType` 
Whether this edge is EMPTY, KELP or PORTAL.
`get_portal_id() -> int` 
The id this portal shares with its far edge, or -1 if this edge is not a portal.
## Controller 
Your dragon: what it sees, how long it is, and the commands it sends this turn. 

`get_length() -> int` 
Segments your dragon has, counting the head.
`get_unit_count() -> int` 
Dragons your team has alive, this one included.
`get_head() -> DragonPart` 
Your dragon's head segment.
`get_id() -> int` 
Your dragon's id, which is also its place in the turn order.
`get_team() -> Team` 
The team you play for.
`get_dir() -> Direction` 
The way your head points at the start of this turn.
`get_vision() -> Vision` 
The tiles around your head.
`get_tiles() -> list[Tile]` 
All 49 tiles in view, row by row from the top left.
`get_tile(pos: Position) -> Tile | None` 
The tile at pos, or None if pos is outside the window.
`get_position() -> Position` 
Your head's position, already wrapped into the map.
`make_move(direction: Direction)` 
Steps one tile that way; of everything you send, the last action is the one applied.
`make_moves(directions: list[Direction])` 
Sprints one step per direction in the list.
`can_split(child_size: int) -> bool` 
Whether a child of that many segments is legal this turn.
`do_split(child_size: int)` 
Splits that many segments off your tail.
`output_log(*message)` 
Prints a line that shows against this turn in the replay.
`draw_indicator_dot(pos: Position, r: int, g: int, b: int)` 
Draws a dot on the replay's board, which changes nothing in the game.
`draw_indicator_line(start: Position, end: Position, r: int, g: int, b: int)` 
Draws a line on the replay's board.
`set_indicator_string(message: str)` 
Labels your dragon with this text for the turn.
`get_sonar_messages() -> list[int]` 
Values that reached you since your last turn, in the order they were sent.
`get_sonar_echoes() -> SonarEchoes` 
`send_sonar(direction_or_message: Direction | int, message: int | None = None) -> bool` 
## helper 
The module itself, imported as unswbc. 

`init() -> tuple[Controller, Game]` 
Reads the spawn block and returns your (controller, game), which stay valid all match.
`update(controller: Controller, game_state: Game) -> bool` 
Reads the next turn into the controller, and returns False once the game is over or your dragon has died.
`end_turn()` 
Ends the turn and flushes everything you printed. 
PreviousStandard Library NextIO Protocol

======== protocol ========



# Wire protocol 
Version 3.0.0. The engine writes plain text to the bot's stdin and reads plain text from its
    stdout. The helper library only parses and prints this format and may be replaced. Blank lines may be skipped, and
    the C and C++ helpers strip anything after `#`. 
## Init block 
Sent once, when the process starts. 

```
ID 0
TEAM A
MAP 11 11
UNIT_LIMIT 64
```

- `ID`: the dragon's id, unique within the game. 
- `TEAM`: `A` or `B`. 
- `MAP`: width, then height. 
- `UNIT_LIMIT`: the maximum number of living dragons per team. Default 64. 
A split child and a restarted bot each receive their own init block. It marks the start of a process, not the start
    of the game. 
## Round block 
Sent once per turn, in this order. 
### 1. Header 

```
ROUND 12
DIR N
LENGTH 5
UNIT_COUNT 2
NUM_MSGS 1
90210
ECHOES 0 1 0 0 1
```

`ROUND`, `DIR` (the dragon's facing), `LENGTH`, `UNIT_COUNT` (living
    dragons on the dragon's team), and `NUM_MSGS`, followed by that many lines, each one unsigned 64-bit sonar value. Then `ECHOES` and five counts of what the dragon's sonars stopped
    on last turn: kelp, ally, ally head, enemy, enemy head. A ray that stopped on nothing adds no count. A dragon gets `ECHOES` and values above 4294967295 only after its reply has had `PROTOCOL 3`. 
### 2. Tiles, 49 lines 

```
2 1 0 -1
3 1 1 8
4 1 0 3
...
```

The 7×7 window, row by row. Row 0 is three tiles north of the head, and column 0 is three tiles west. Each line is `x y hasPearl pearlIn`. Coordinates are already wrapped. `pearlIn` is −1 for a tile that never spawns. 
### 3. Dragon bodies 

```
DRAGON_BODIES 4
A 0 5 4 N 1
A 0 5 5 N 0
B 1 7 3 W 1
B 1 8 3 W 0
```

`DRAGON_BODIES` and a count, then one line `team id x y facing isHead` for every segment of every
    living dragon inside the window, the bot's own included. A head's facing is its heading; a body segment's facing points
    towards the head. 
### 4. Horizontal edges, 8 lines of 7 

```
. . . . . . .
. . w w . . .
. . . . . . .
. . . . . . .
. . . 3 . . .
. . . . . . .
. . . . . . .
. . . . . . .
```

The north edge of each row of tiles, then the south edge of the last row. 
### 5. Vertical edges, 7 lines of 8 

```
. . . . . . . .
. . . . . . . .
. w . . . . . .
. . . . . . . .
. . . . . . . .
. . . . . . . .
. . . . . . . .
```

The west edge of each column of tiles, then the east edge of the last column. 
### Edge symbols 

 | Symbol | Meaning
 | `.` | Open.
 | `w` | Kelp.
 | An integer | A portal. The other edge with that id is the far end. 
Split on whitespace, since a portal id may have more than one digit. 
## Reply 
Lines to stdout, ending with `ENDTURN`, then a flush. One flush per turn is the cheap way (see Timeouts). 

 | Command | Arguments | Effect
 | `MOVE` | `N`/`E`/`S`/`W` letters | Action: move. `MOVE NNE` is three steps.
 | `SPLIT` | Segment count | Action: split off the rear segments.
 | `SONAR` | `N`/`E`/`S`/`W`, then an unsigned 64-bit integer | Ping that way after the action.
 | `PROTOCOL` | `3` | Protocol 3 input from the next turn.
 | `INDICATOR` | Text | Label on the dragon this turn.
 | `LOG` | Text | Message attached to this turn.
 | `DOT` | `x y r g b` | Dot on the board in the replay.
 | `LINE` | `x1 y1 x2 y2 r g b` | Line on the board in the replay.
 | `ENDTURN` |  | Stop reading. 
`MOVE` and `SPLIT` overwrite each other, a `SONAR` overwrites an earlier one in the
    same direction, and `INDICATOR` overwrites `INDICATOR`. The last one read takes effect. The
    drawing commands are applied as they are read. See Execution order for the exact sequence. 
## Ending 
When the game ends for a dragon, the engine closes its stdin. Reading a line then returns EOF, and the process
    should exit. The helpers also stop on `ENDGAME`, which the current engine does not send. 
PreviousHelper Reference NextProtocol Upgrade

======== protocol-upgrade ========



# Protocol upgrade 
The protocol is now version 3. It adds directed 64-bit sonar and echoes. Bots written for version
    2 still run without change, but they cannot use the new features. 
## What changed 

 |  | Protocol 2 | Protocol 3
 | Sending | `SONAR <value>`: one 32-bit value, in the facing direction | `SONAR <N|E|S|W> <value>`: up to four 64-bit values, one for each direction
 | Receiving | Only values up to 4294967295. Larger values are left out. | All values
 | Echoes | None | `ECHOES` line after the messages 
Both forms of `SONAR` work at every version. The version only controls what the engine sends to your bot. 
## Upgrading with a helper 
Update the toolkit, then run `unswbc update` on your bot folder. It replaces only the helper files with
    the ones the toolkit ships, and keeps each old helper as a `.bak` file next to it. Your main file is not changed. 

```
unswbc update my_bot
```

The new helper asks for protocol 3 in `end_turn`. Old sonar calls still work: `send_sonar(message)` (in C, `unswbc_send_sonar(message)`) still sends a 32-bit value in the
    facing direction. For the new features, use `send_sonar(direction, message)` and `get_sonar_echoes` (in C, `unswbc_send_sonar_to` and `unswbc_sonar_echoes`). 
Sonar messages are now 64-bit. If your code names the message type as 32-bit, for example `std::vector<std::uint32_t>` in C++ or `uint32_t const *` in C, change it to `std::uint64_t` or `uint64_t`, or use `auto`. 
## Upgrading a bot without a helper 
- Print `PROTOCOL 3` before `ENDTURN`. It applies from your next turn. 
- Read sonar values as unsigned 64-bit integers. 
- After the messages, read the `ECHOES` line: five counts, in the order kelp, ally, ally head, enemy, enemy
        head. 

```
SONAR E 1806
SONAR W 18446744073709551615
MOVE N
PROTOCOL 3
ENDTURN
```

On your next turn, the sonar part of the round block can look like this: one message from another dragon, and one
    echo each for kelp and an enemy head. 

```
NUM_MSGS 1
90210
ECHOES 1 0 0 0 1
```

## Details 
- A dragon made by a split keeps the protocol version of the dragon it split from. 
- A dragon on protocol 2 never gets a value above 4294967295. The engine does not keep it for later. 
- If you send a value above 4294967295, a bot on protocol 2 cannot receive it. Keep a value at 32 bits if an older
        bot must read it. 
PreviousIO Protocol NextMap Files

======== map-files ========



# Map Files 
A `.map` file is plain text with one directive per line. The map editor writes them,
    and they can be written by hand. 

```
MAP 11 11
SYMMETRY xy
TILE_COUNT 81
TILE 1 1 8 20
TILE 2 1 8 20
...
EDGE_COUNT 44
EDGE 0 1 -1
EDGE 12 2 3
...
DRAGON_COUNT 2
DRAGON 0 3 3 5 2 5 1 5
DRAGON 1 3 7 5 8 5 9 5
```

## Directives 
### `MAP width height` 
Required, and first. Everything after it is bounds-checked against it. 
### `MAP_NAME name` 
Optional. The visualiser and the VS Code viewer display this name. 
### `SYMMETRY x | y | xy` 
Optional. Mirrored tiles share one pearl countdown and fire together, so a symmetric map is fair in
    timing as well as in shape. See Pearl spawns. 
### `TILE x y minGap maxGap` 
The tile's spawn range. After each attempt the tile draws its next countdown uniformly from `minGap` to `maxGap` inclusive. A `maxGap` of 0 means the tile never spawns; otherwise `minGap` must be at least 1. `TILE_COUNT` must match the number of `TILE` lines, and likewise `EDGE_COUNT` and `DRAGON_COUNT`. 
### `EDGE index kind portalId` 

 | Kind | Meaning
 | `0` | Open.
 | `1` | Kelp. Portal id `-1`.
 | `2` | Portal. Exactly two edges share the id, and both must have the same orientation. 
Rows of horizontal edges (a tile row's north side) alternate with rows of vertical edges (its west side). Each row
    is `width + 1` long, so the index runs to `(2·height + 1)·(width + 1)`. The editor is the
    practical way to write these. 
### `DRAGON team segmentCount x y x y ...` 
`team` is `0` (A) or `1` (B), followed by the segment count and then the
    coordinates, head first. Segments must be adjacent, on the board, and must not overlap another dragon. The
    minimum length is 2. Ids are assigned in file order from 0, which is also turn order. 
PreviousProtocol Upgrade NextAPI

======== api ========



# API 
Everything a team does on Submissions and Battles, and everything readable on the site, is available over JSON. Team
    settings, membership and accounts are changed on the site only. 
## Keys 
Each member makes one key on Team. It acts as that member, so uploads and challenges are
    recorded against their name. The key is shown once. Making a new one replaces the old one, leaving the team deletes
    it, and the leader can revoke any member's key. 

```
curl https://game.battlecode.au/api/v1/me \
  -H "Authorization: Bearer bc_..."
```

## Conventions 
The base URL is `https://game.battlecode.au/api/v1`. Requests and responses are JSON, except the upload
    (multipart) and the two downloads. Errors carry the status and a message: 

```
{ "error": "That API key is not valid. Make a new one on your team page." }
```

The limits are 120 requests a minute per key, and 30 a minute for `/leaderboard` and `/ratings`, the two expensive reads. Over either limit the response is 429 with a `Retry-After` header. An unknown path under `/api/v1` is a 404 in the same shape, never an HTML page. 
## You and your team 
- `GET /me` your user, team and key. 
- `GET /team` your team with members, record, rank and pending join requests. 
## Submissions 
- `GET /submissions` every version with wins, draws and losses. 
- `POST /submissions` upload a version. Fields: name, language (python, c, cpp), description, zip. The zip
        is 4 MB at most, and a team can upload 12 versions an hour. 
- `GET /submissions/:id` one version with its build log. 
- `GET /submissions/:id/download` the zip you uploaded. 
- `POST /submissions/:id/activate` make it the version that plays. 

```
curl https://game.battlecode.au/api/v1/submissions \
  -H "Authorization: Bearer $KEY" \
  -F name=v12 -F language=python -F description="wider search" -F [email protected]
```

```
curl -X POST https://game.battlecode.au/api/v1/submissions/42/activate \
  -H "Authorization: Bearer $KEY"
```

## Battles 
- `GET /battles?limit=50` your battles, newest first, up to 200. 
- `POST /battles` challenge a team. Body: teamId, ranked, mapIds (optional). Ranked is a five-game series.
        Same limits as the site: 60 games an hour, and a separate 60 against dev teams. 
- `GET /battles/:id` one battle with its games and, if unplayed, its place in the queue. For your own
        battles it also carries `log`, which says why a game crashed or timed out. 
- `GET /battles/:id/replay` the replay file, once the game has finished. Answers with a redirect to a
        temporary download link, so follow redirects (`curl -L`). The link carries its own signature, so your
        key must not be sent on to it. curl drops the header across hosts for you; some clients keep it, Python's `urllib` among them, and the download is refused. 

```
curl https://game.battlecode.au/api/v1/battles \
  -H "Authorization: Bearer $KEY" -H "Content-Type: application/json" \
  -d '{ "teamId": 7, "ranked": false, "mapIds": [3] }'
```

```
{ "ids": [1201] }
```

```
curl -L -o 1201.replay https://game.battlecode.au/api/v1/battles/1201/replay \
  -H "Authorization: Bearer $KEY"
```

## Reads 
- `GET /leaderboard` the ladder as the Leaderboard page shows it: rating history, members, institutions and
        prize eligibility. 
- `GET /ratings` the same ladder, ratings and history only. 
- `GET /teams` teams listed on Find a team. 
- `GET /teams/:id` a team's public profile and recent battles. 
- `GET /tournaments` and `GET /tournaments/:id` tournaments and brackets. 
- `GET /maps` the maps in play, with their text. 
- `GET /queue` live judge capacity and queue length. 
PreviousMap Files 