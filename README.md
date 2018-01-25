# Halite 2 mlomb-bot

**(in progress)**

## Client
I’ve replaced the starter kit with my own classes. I feel like recreating the whole map every turn was unnecessary and it doesnt let me save information between turns inside an specific entity.

## Tasks
There are tasks that ships should complete. Task types are: `SUICIDE` (not used), `ATTACK`, `WRITE`, `DEFEND`, `ESCAPE` and `DOCK`. Each task must have a target location. Task can optionally have a target entity and a max_ships value and some other information relative to that specific task.

At the beginning of the turn I generate all the possible tasks like this:
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
  

## Map

## Navigation
I think this is the most inefficient way to do navigation

## Rush defense

## Writing

## Disclosure
I’m not a native english, so maybe I’ll have some errors.
