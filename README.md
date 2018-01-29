# Halite 2 [mlomb-bot](https://halite.io/user/?user_id=5622)

In this write up  I'll basically explain my bot's logic in one turn and some important decisions that I took while coding it.

## Client
I’ve replaced the starter kit with my own classes. I feel like recreating the whole map every turn was unnecessary and it doesnt let me save information between turns inside an specific entity.

## Task system
I based my entrie high level strategy using a tasks system. This is how it works:
There are tasks that ships should complete. Task types are: `SUICIDE` (not used), `ATTACK`, `WRITE`, `DEFEND`, `ESCAPE` and `DOCK`. Each task must have a target location. A task can optionally have a target entity and a max_ships value and some other information relative to that specific task.

*Tip: If you are using [Chlorine](https://github.com/fohristiwhirl/chlorine) with my messages.json you can check the assigned task of this ship with the Angle Messages feature.*

### Generating tasks
At the beginning of the turn I generate all the tasks for this turn like this [(Instance.cpp:473)](../master/Latest/Instance.cpp#L473):
- For each planet on the map
  - Create a `DOCK` task
    - `location` is set to the planet's location
    - `target entity` is set to the planet's entity_id
    - `max_ships` is set to the the planet's docking spots *(if `game_over` then this is 0)*
    
- if **not** `game_over`
  - For each docked friendly ship
    - If the distance to the closest enemy ship is less than `MAX_SPEED + WEAPON_RADIUS + 1`
      - If we don't already have created a task to defend from this ship
        - Create a `DEFEND` task
      - If this docked ship is closer to this task's closest enemy ship
        - `location` is set to `enemyShip`.closestPointTo(`dockedShip`, 1 *(fudge)*) **this is very important because the ships defending will stay between the enemy ship and the docked ship**
        - `max_ships` is set to the number of enemy ships near the closest enemy ship in a radius of 5 *(im not sure about this)*

  - For each enemy ship
    - *4p* If the owner of this ship have 0 planets and `turn > 50` we assume that they surrendered so we don't create a task for them unless they are too close (35 units) from one of my planets.
      - Create a `ATTACK` task
        - `location` is set to the ship's location
        - `target entity` is set to the ship's entity_id
        - `indefense` is set to `true` if the ship is not commandable (probably docked)
        - `max_ships` is set to `4` if the ship is indefense, `3` otherwise
  
  - if `writing`
    - For each dot required to write the message
      - Create a `WRITE` task
        - `location` is set to this dot location
        - `max_ships` is set to `1`
        
  
- if `game_over`
  - For each corner in the map (usually 4)
    - Create a `ESCAPE` task
      - `location` is set to the corner location
      - `max_ships` is set to -1 *(infinite ships)*

### Assigning tasks
We've now created all the tasks but now we need to assign to each friendly ship a task. Some ships have a fixed task, like docked ships, so we manually assign the tasks for those ships. For the rest of the ships we use a priority system.

First we put all the (friendly) undocked ships inside a queue. This queue will contain all the ships that don't have a task assigned.
- While the queue is not empty [(Instance.cpp:745)](../master/Latest/Instance.cpp#L745)
  - Take a ship from the queue
  - `maxPriority`, `priorizedTask`, `otherShipPtrOverriding` are initialized
    - For each task
      - Calculate the **relative priority** of this ship with this task
      - If the task is full (ships assigned >= `max_ships`) we search in the ships assigned for a ship with the minimum relative priority less than this ship's relative priority (this means that for this ship this task is more important to it than the other one found) if we found one we save it into `otherShipPtrOverriding` or we just skip this task.
      - If the priority > `maxPriority` we update `maxPriority`, `priorizedTask` and `otherShipPtrOverriding` if we should override a ship to assign this task.
    - We assign the `priorizedTask` to this ship
    - If we were overriding a ship in a task we remove that ship from the task and push it to the queue again

Sometimes ships can't find a suitable task, so at the end I just assign them to the closest `ATTACK` or `DEFENND` tasks.

#### Relative priority
To calculate the priority of a ship for a task I calculate the distance from the ship to the target location and assign it to a variable `d` [(Instance.cpp:765)](../master/Latest/Instance.cpp#L765). Then I modify this variable depending on the task type like so *(very rough idea, the code will explain it better)*:
- `DOCK` task:
  - `d += 5`
  - Subtract `d` so it prefers outer planets
  - If there are 0 nearby enemies in a radius of 35 unis `d -= 15`
  - If we are very close to the target and there are not friendly ships able to defend us `d += 1000` (that's a nope)
- `DEFEND` task:
  - `d -= 25` very important
  - If the enemy is too close to the docked ships its much more important
- `ATTACK` task:
  - `d -= 5`
  - if `indefense`
    - `d -= 5`
    - If there are more friendly ships close to this enemy ship it's more important
- `WRITE` task:
  - `d += 40` it's not important

Then `priority = 100 - d / 100`.

## Compute Move
Given the task assigned each ship should issue a `Dock`/`Undock` move or create a `NavigationRequest` *(or do nothing)*.

A `NavigationRequest` [(Navigation.hpp:32)](../master/Latest/Navigation.hpp#L32) represents where and how a ship wants to navigate to a location. Basically it stores this:
- A reference to the ship
- The desired location
- `avoid_enemies` if the navigation should avoid near enemies while navigating

Computing the move basically occurs this way [(Ship.cpp:80)](../master/Latest/Ship.cpp#L80):

- If the assigned task type is `ATTACK`:

  We're going to issue a `NavigationRequest` with `avoid_enemies=false` and the target location will vary:
    - If the target ship is a undocked ship the target location will be calculated as the closest point from the ship to the target ship 3 units away.
    - If the target ship is a docked ship the target location will be calculated as follows:
      - `Vector2 point = shipTarget->location + Vector2::Velocity(docked_planet->location.OrientTowardsRad(shipTarget->location), 10)`
      - `Vector2 target= point.ClosestPointTo(shipTarget->location, shipTarget->radius, 1)`
      
      This way we obtain a point 10 units away from the target ship in the opposite direction facing the planet. Then we obtain the closest point 1 unit away as the target location. This is to avoid getting stucked between the planet and a docked ship.
      
      ![DockAttack](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/dock_attack.png)
      
- If the assigned task type is `DEFEND`:
  - Issue a `NavigationRequest` with `avoid_enemies=false`.
  
    If we are very close (7 units) to the ship that we are defending from we just attack the ship, the target location will be the closest point from the ship to the enemy ship. If not, the target location will be the task location, to stay between the defended ship and the enemy ship.
    
    ![AttackDocked](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/attack_docked.png)
- If the assigned task type is `DOCK`:
  - If we are not docking or undocking
  - If we are in `rush_phase` we define `threatened` as `threats != 0 && threats >= friends` where `threats` is the number of enemy ships inside `(TurnsToBeUndocked() + 1) * hlt::constants::MAX_SPEED + hlt::constants::WEAPON_RADIUS` and `friends` the same thing but for friendly ships.
  - If we are docked and we are `threatened` or we surrender in 4p we issue a `Undock` move.
  - If we can dock to the task's target
    - If we are not threatened we issue a `Dock` move.
    - else we are `threatened` or `waiting_for_write` so we will stay inside the docking area but away from the closest enemy as possible [(Ship.cpp:473)](../master/Latest/Ship.cpp#L135). [Here](https://halite.io/play/?game_id=9679885) is a game showcasing this case.
  - If none of those conditions are met, we just continue navigating to the planet, so we issue a `NavigationRequest` with `avoid_enemies=true` and the location as the closest point from the ship to the planet.

- If the assigned task type is `ESCAPE` or `WRITE`:
  - Just issue a `NavigationRequest` with `avoid_enemies=true` and the target location as the task location.


## Map
I represented the map as a grid where each map unit were divided into `MAP_DEFINITION` (4) map cells. Each turn the map is cleared and generated again.

Each map cell contains:
- `bool solid`
- `int nextTurnEnemyShipsTakingDamage`
- `int nextTurnFriendlyShipsTakingDamage`
- `int nextTurnEnemyShipsAttackInRange`
- `int nextTurnFriendlyShipsAttackInRange`
  
To generate the map first we iterate over all cells covered by a planet and mark them as solid [(Map.cpp:24)](../master/Latest/Map.cpp#L24).

After that, we add all ships into the map, this also include the ships that will spawn next turn near a planet to increase the accuarity.

For each ship, depending on whether they are commandable or are undocked ships, friendly ships or enemy ships we fill the cell's values like this [(Map.cpp:121)](../master/Latest/Map.cpp#L121):
- First we select the radius we will iterate this ship:
  - friendly and undocked: `SHIP_RADIUS + MAX_SPEED`
  - friendly and docked *(or frozen)*: `SHIP_RADIUS + WEAPON_RADIUS`
  - enemy and undocked: `SHIP_RADIUS + MAX_SPEED + WEAPON_RADIUS + 1`
  - enemy and docked *(or frozen)*: `SHIP_RADIUS + WEAPON_RADIUS`

Frozen ships are ships that will not move in this turn like ghost ships (ships that will spawn next turn) or ships that the navigation decided not to move.

- After that, for each cell in this radius we increment the appropriate values:
  - friendly and undocked: `nextTurnFriendlyShipsTakingDamage` and `nextTurnFriendlyShipsAttackInRange`
  - friendly and docked `nextTurnFriendlyShipsTakingDamage`
  - enemy and undocked: `nextTurnEnemyShipsTakingDamage` and `nextTurnEnemyShipsAttackInRange`
  - enemy and docked *(or frozen)*: `nextTurnEnemyShipsTakingDamage`

These are some images of the map after filling it (displaying `nextTurnFriendlyShipsAttackInRange` and `nextTurnEnemyShipsAttackInRange`):

![Map2](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/map_2.bmp)

![Map1](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/map_1.bmp)

With all this information, we can now perform navigation.

## Navigation and Combat
My navigation and combat work very tight together. I choose where to move a ship depending on the number of ships sorrounding and the distance to the desired target. No clustering going on.

My navigation works like this [(Navigation.cpp:135)](../master/Latest/Navigation.cpp#L135):
- Score all the possible movements (each one called `NavigationOption`) of every `NavigationRequest` (yes, thats (360 * 7 + 1) per ship) using the map and the `avoid_enemies` flag (more on this later).
- Each ship sorts its `NavigationOption`s and sets `optionSelected` to `0` so they point to the best option.
- We insert all the `NavigationRequest`s inside a queue.
- While the queue is not empty
  - Sort the queue so the ship with the least priority is first
  - Pick the `NavigationRequest` on the top and remove it from the queue
  - Set `optionSelected` to 0 for this ship
  - Iterate over all options incrementing `optionSelected` until no conflicts are found with all the other `NavigationRequest`s like collisions.
  - If we run out of options, this ship will stand still
    - Update the map so this ship is now frozen
    - Remove this ship from the `NavigationRequest`s set so its never processed again
    - Recalculate the scores for near `NavigationRequest`s
    - Add all the near `NavigationRequest`s to the queue again

This process have some anti-timeout checks that will execute all the selected navigation options even if conflicts still exists because we run out of time.

### Scoring the `NavigationOption`s

This occurs inside CalculateScores [(Navigation.cpp:80)](../master/Latest/Navigation.cpp#L80) where we iterate all the `NavigationOption`s of all the `NavigationRequest`s. For each option, we check if there is static collision using `max_thrusts` (`max_thrusts` is calculated before calcuating the map *(and when modifing the map)* and it contains the max thrust value this ship can issue in a determinated angle, due ship-planet and ship-docked_ship collisions). If there is a problem, score will be set to `-99` if not, it will be delegated to `GetPositionScore`.
`GetPositionScore` will calculate the score using the corresponding map cell information like this:
- If `avoiding_enemies`
  - `(100 - nextTurnEnemyShipsAttackInRange) * 10000 + MAX_DISTANCE - optionPosition.DistanceTo(targetLocation)`
- else *(not avoiding enemies)*
  - If `nextTurnFriendlyShipsTakingDamage > nextTurnEnemyShipsAttackInRange`
    - `nextTurnEnemyShipsTakingDamage * 10000 + MAX_DISTANCE - optionPosition.DistanceTo(targetLocation)`
  - else *(nope)*
    - `-99`

Basically, if we are avoiding enemies, we will prefer the option that have the less enemies in range next turn and then the closest one to the target.
If we are attacking, first we check if the ships that can reach to this point are more than the enemy ships can reach and attack us. If thats possible, we'll maximize `nextTurnEnemyShipsTakingDamage` and then by proximity to the target.

## Writing
![Halite Message](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/halite-message.png)

Here is an [example game](https://halite.io/play/?game_id=9680808).

I did this because it was simple to implement and it shouldn't have any impact on the game. Although I have to say that sometimes I time out doing this in very rare cases.

Writing things was possible only on won games so I first thought to write "GG" or something like that but it was too short and boring. I just ended up writing "HALITE" using ~45 ships.

The most complicated part was to write the coordinates of each point that form the letters (which you can find in [Instance.cpp:584](../master/Latest/Instance.cpp#L584)) after that it was trivial just add a new task type and lower down the priority.

To know where to position the message I just use a nice bruteforce algorithm ([Instance.cpp:54](../master/Latest/Instance.cpp#L54)) using the map to find the 140x22 rect closest to the center where no solid objects exists.

To start writing the message I detect that the game is won like this:
```
// we should have at least 90 ships
if (myShips > 90) {
  // we own the 85% of the planets or all the planets - 1
  if(myPlanets / totalPlanets > 0.85 || myPlanets >= totalPlanets -1)
    // we won, start writing
  }
}
```
Once we start writing ship docking is disabled 25 turns so we can give it some time to the ships to write the message.

## Debugging

### Logging
The most important way to debug the bot. I tried to log everyting that generates a big decision on the game, like task assignment or target location selections.
At the end it doesnt matter what I log because I disable it with a flag during compilation [(Log.cpp:21)](../master/Latest/Log.cpp#L21).

### A Stopwatch
To measure the time each of my functions were taking I created a simple Stopwatch class [(Instance.hpp:17)](../master/Latest/Instance.hpp#L17) and use it like this:
```cpp
{
  Stopwatch s("Clear and fill the map");
  map->ClearMap();
  map->FillMap();
}
```
So when the Stopwatch object gets out of scope it measure the time between its instantiation and its destruction.
Then at the end of the turn I get something like this:
```
Clear and fill the map: 42ms
```

### Chlorine
[Chlorine](https://github.com/fohristiwhirl/chlorine) created by [fohristiwhirl](https://github.com/fohristiwhirl) was the most important debugging tool I had. I contributed by adding zoom to it. Then I used it to inspect in detail the micro-movements of top players.

I modified it a bit so I could draw some important ranges apart from the ATTACK_RADIUS that was there already. Here is a pic:

![ChlorineRanges](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/chlorine_ranges.png)

The ranges displayed are:
- `SHIP_RADIUS + WEAPON_RADIUS` *(to know the actual attack radius)*
- `SHIP_RADIUS + MAX_SPEED` *(to know where the ship could move next turn)*
- `SHIP_RADIUS + MAX_SPEED + WEAPON_RADIUS` *(to know where this ship's attack can reach next turn)*

With this and watching a lot of replays I managed to build my map grid and my entire combat strategy.

## Thanks
Thanks everyone in Two Sigma for creating this awesome competition. I wasn't here for Halite 1 but definitely I'll be here for the Halite 3, 4...

Thanks to [fohristiwhirl](https://github.com/fohristiwhirl) for creating [Chlorine](https://github.com/fohristiwhirl/chlorine) it really helped me a lot.

Also we had an awesome community on the Discord server, hope to see them next Halite again!
