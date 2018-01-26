# Halite 2 [mlomb-bot](https://halite.io/user/?user_id=5622)

**(in progress)**

## Client
I’ve replaced the starter kit with my own classes. I feel like recreating the whole map every turn was unnecessary and it doesnt let me save information between turns inside an specific entity.

## Task system
There are tasks that ships should complete. Task types are: `SUICIDE` (not used), `ATTACK`, `WRITE`, `DEFEND`, `ESCAPE` and `DOCK`. Each task must have a target location. A task can optionally have a target entity and a max_ships value and some other information relative to that specific task.

*Tip: If you are using [Chlorine](https://github.com/fohristiwhirl/chlorine) with my messages.json you can check the assigned task of this ship with the Angle Messages feature.*

### Generating tasks
At the beginning of the turn I generate all the tasks for this turn like this (Instance.cpp:473):
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
We've now created all the tasks but now we need to assign to each friendly ship a task. Some ships have a fixed task, like docked ships, so we manually assign the tasks for those ships. For rest of the ships we use a priority system.

First we put all the (friendly) undocked ships inside a queue. This queue will contain all the ships that doesnt have a task assigned.
- While the queue is not empty (Instance.cpp:745)
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
To calculate the priority of a ship for a task I caulcuate the distance from the ship to the target location and assign it to a variable `d` (Instance.cpp:765). Then I modify this variable depending on the task type like so *(very rough idea, the code will explain it better)*:
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
    - If there are more friendly ships close to this enemy ship its more important
- `WRITE` task:
  - `d += 40` its not important

Then `priority = 100 - d / 100`.
## Map

## Navigation
I think this is the most inefficient way to do navigation

## Game over

## Rush defense

## Writing
![Halite Message](https://raw.githubusercontent.com/mlomb/halite2-bot/master/imgs/halite-message.png)

Here is an [example game](https://halite.io/play/?game_id=9403140).

I did this because it was simple to implement and it shouldn't have any impact on the game. Although I have to say that sometimes I time out doing this in very rare cases.

Writing things was possible only on won games so I first thought to write "GG" or something like that but it was too short and boring. I just ended up writing "HALITE" using ~45 ships.

The most complicated part was to write the coordinates of each point that form the letters (which you can find in Instance.cpp:584) after that it was trivial just add a new task type and lower down the priority.

To know where to position the message I just use a nice bruteforce algorithm (Instance.cpp:54) using the map to find the 140x22 rect closest to the center where no solid objects exists.

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

## Thanks
Thanks everyone in Two Sigma for creating this awesome competition. I wasn't here for Halite 1 but definitely I'll be here for the Halite 3, 4...

Thanks to [fohristiwhirl](https://github.com/fohristiwhirl) for creating [Chlorine](https://github.com/fohristiwhirl/chlorine) it really helped me a lot.

Also we had an awesome community on the Discord server, hope to see them next Halite again!
