"""Smoke test: drive CapriceGymEnv with random actions and report throughput.

Run from the project root (or anywhere -- capriceenv.py resolves paths
relative to the project root itself):

    pip install -r python/requirements.txt
    python python/random_agent_demo.py
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from capriceenv import CapriceGymEnv


def main():
    env = CapriceGymEnv()
    obs, info = env.reset()
    print(f"Observation shape: {obs.shape}, action space: {env.action_space} ({env.action_names})")

    n_steps = 200
    start = time.time()
    for _ in range(n_steps):
        action = env.action_space.sample()
        obs, reward, terminated, truncated, info = env.step(action)
        if terminated or truncated:
            obs, info = env.reset()
    elapsed = time.time() - start
    print(f"Ran {n_steps} steps in {elapsed:.2f}s ({n_steps / elapsed:.1f} steps/sec)")

    from PIL import Image
    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "random_agent_frame.png")
    Image.fromarray(obs).save(out_path)
    print(f"Saved final frame to {out_path}")

    env.close()


if __name__ == "__main__":
    main()
